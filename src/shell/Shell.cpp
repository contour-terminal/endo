// SPDX-License-Identifier: Apache-2.0
#include "Shell.hpp"

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <crispy/assert.h>
#include <crispy/utils.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "Error.hpp"
#include "OutputParser.hpp"
#include "Pipe.hpp"
#include "Platform.hpp"
#include "Process.hpp"
#include "ProcessGroup.hpp"
#include "Prompt.hpp"
#include "TTY.hpp"
#include "TableFormatter.hpp"
#include "commands/JobsCommand.hpp"
#include "commands/LsCommand.hpp"
#include "commands/PsCommand.hpp"
#include "platform/LinuxFileInfoProvider.hpp"
#include "platform/LinuxProcessProvider.hpp"
#include <endo-language/ASTPrinter.hpp>
#include <endo-language/IRGenerator.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>
#include <endo-language/Parser.hpp>

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <poll.h>
    #include <pwd.h>
    #include <unistd.h>
#endif

namespace
{
// Use centralized log categories from LogCategories.hpp
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

std::string processEscapeSequences(std::string_view input)
{
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' && i + 1 < input.size())
        {
            char next = input[i + 1];
            switch (next)
            {
                case '\\':
                    result += '\\';
                    ++i;
                    break;
                case 'a':
                    result += '\a';
                    ++i;
                    break;
                case 'b':
                    result += '\b';
                    ++i;
                    break;
                case 'e':
                    result += '\x1B';
                    ++i;
                    break;
                case 'f':
                    result += '\f';
                    ++i;
                    break;
                case 'n':
                    result += '\n';
                    ++i;
                    break;
                case 'r':
                    result += '\r';
                    ++i;
                    break;
                case 't':
                    result += '\t';
                    ++i;
                    break;
                case 'v':
                    result += '\v';
                    ++i;
                    break;
                case '0': {
                    // Octal: \0, \0n, \0nn, \0nnn
                    ++i; // skip backslash
                    ++i; // skip '0'
                    int value = 0;
                    int digits = 0;
                    while (i < input.size() && digits < 3 && input[i] >= '0' && input[i] <= '7')
                    {
                        value = value * 8 + (input[i] - '0');
                        ++i;
                        ++digits;
                    }
                    --i; // compensate for loop increment
                    result += static_cast<char>(value);
                    break;
                }
                case 'x': {
                    // Hex: \xH, \xHH
                    ++i; // skip backslash
                    ++i; // skip 'x'
                    int value = 0;
                    int digits = 0;
                    while (i < input.size() && digits < 2)
                    {
                        char c = input[i];
                        if (c >= '0' && c <= '9')
                            value = value * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f')
                            value = value * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                            value = value * 16 + (c - 'A' + 10);
                        else
                            break;
                        ++i;
                        ++digits;
                    }
                    --i; // compensate for loop increment
                    if (digits > 0)
                        result += static_cast<char>(value);
                    else
                        result += "\\x"; // invalid escape, keep literal
                    break;
                }
                default:
                    // Unknown escape - keep literal
                    result += input[i];
                    break;
            }
        }
        else
        {
            result += input[i];
        }
    }
    return result;
}

/// Recursively converts a runtime value (number, tuple, list, option, etc.) to a printable string.
std::string valueToString(uint64_t rawVal, CoreVM::Runner* runner)
{
    if (runner && runner->isKnownObject(rawVal))
    {
        auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawVal));
        auto const typeId = obj->type->id;
        if (typeId == CoreVM::BuiltinTypeId::List)
        {
            std::string result = "[";
            bool first = true;
            while (obj && obj->type->id == CoreVM::BuiltinTypeId::List && obj->tag == 1)
            {
                if (!first)
                    result += "; ";
                first = false;
                result += valueToString(obj->getSlot(0), runner);
                obj = reinterpret_cast<CoreVM::TypedObject*>(obj->getSlot(1));
            }
            result += "]";
            return result;
        }
        if (typeId == CoreVM::BuiltinTypeId::Tuple2)
        {
            return "(" + valueToString(obj->getSlot(0), runner) + ", "
                   + valueToString(obj->getSlot(1), runner) + ")";
        }
        if (typeId == CoreVM::BuiltinTypeId::Tuple3)
        {
            return "(" + valueToString(obj->getSlot(0), runner) + ", "
                   + valueToString(obj->getSlot(1), runner) + ", " + valueToString(obj->getSlot(2), runner)
                   + ")";
        }
        if (typeId == CoreVM::BuiltinTypeId::Option)
        {
            if (obj->tag == 0)
                return "None";
            return "Some " + valueToString(obj->getSlot(0), runner);
        }
        if (typeId == CoreVM::BuiltinTypeId::Result)
        {
            if (obj->tag == 0)
                return "Error " + valueToString(obj->getSlot(0), runner);
            return "Ok " + valueToString(obj->getSlot(0), runner);
        }
        if (obj->type->kind == CoreVM::TypeKind::Product)
        {
            std::string result = "{ ";
            for (size_t i = 0; i < obj->type->fields.size(); ++i)
            {
                if (i > 0)
                    result += "; ";
                result += obj->type->fields[i].name;
                result += " = ";
                auto slotVal = obj->getSlot(static_cast<uint8_t>(i));
                switch (obj->type->fields[i].type)
                {
                    case CoreVM::LiteralType::String: {
                        auto const* str =
                            reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slotVal));
                        result += str ? *str : "(null)";
                        break;
                    }
                    case CoreVM::LiteralType::Boolean: result += slotVal ? "true" : "false"; break;
                    default: result += std::to_string(static_cast<int64_t>(slotVal)); break;
                }
            }
            result += " }";
            return result;
        }
        return std::to_string(static_cast<int64_t>(rawVal));
    }
    return std::to_string(static_cast<int64_t>(rawVal));
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
        ssize_t const n = read(tty.inputFd(), &ch, 1);
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
// Environment implementation
// ========================================================================

void Environment::setAndExport(std::string_view name, std::string_view value)
{
    set(name, value);
    exportVariable(name);
}

// ========================================================================
// TestEnvironment implementation
// ========================================================================

void TestEnvironment::set(std::string_view name, std::string_view value)
{
    _values[std::string(name)] = std::string(value);
}

std::optional<std::string_view> TestEnvironment::get(std::string_view name) const
{
    if (auto i = _values.find(name.data()); i != _values.end())
        return i->second;
    else if (auto const* value = getenv(name.data()))
        return std::string_view { value };
    else
        return std::nullopt;
}

void TestEnvironment::unset(std::string_view name)
{
    _values.erase(std::string(name));
    unsetenv(name.data());
}

void TestEnvironment::exportVariable(std::string_view name)
{
    if (auto i = _values.find(name.data()); i != _values.end())
        setenv(name.data(), i->second.data(), 1);
}

std::vector<std::string> TestEnvironment::keys() const
{
    std::vector<std::string> result;
    result.reserve(_values.size());
    for (auto const& [key, _]: _values)
        result.push_back(key);
    return result;
}

// ========================================================================
// SystemEnvironment implementation
// ========================================================================

void SystemEnvironment::set(std::string_view name, std::string_view value)
{
    _values[std::string(name)] = std::string(value);
}

std::optional<std::string_view> SystemEnvironment::get(std::string_view name) const
{
    if (auto i = _values.find(name.data()); i != _values.end())
        return i->second;
    else if (auto const* value = getenv(name.data()))
        return std::string_view { value };
    else
        return std::nullopt;
}

void SystemEnvironment::unset(std::string_view name)
{
    _values.erase(std::string(name));
    unsetenv(name.data());
}

void SystemEnvironment::exportVariable(std::string_view name)
{
    if (auto i = _values.find(name.data()); i != _values.end())
        setenv(name.data(), i->second.data(), 1);
}

std::vector<std::string> SystemEnvironment::keys() const
{
    std::vector<std::string> result;

    // First, collect from system environment
    for (char** env = ::environ; *env != nullptr; ++env)
    {
        std::string_view entry(*env);
        if (auto pos = entry.find('='); pos != std::string_view::npos)
            result.emplace_back(entry.substr(0, pos));
    }

    // Add locally-set variables that might not be exported yet
    for (auto const& [key, _]: _values)
    {
        if (std::find(result.begin(), result.end(), key) == result.end())
            result.push_back(key);
    }

    return result;
}

SystemEnvironment& SystemEnvironment::instance()
{
    static SystemEnvironment env;
    return env;
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
            if (entry.openedFd == -1)
            {
                int const oflags =
                    entry.append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                auto const result = pm.openFile(entry.path, oflags);
                if (result.has_value())
                    entry.openedFd = result.value();
            }
            if (entry.openedFd != -1)
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
            if (entry.openedFd == -1)
            {
                auto const result = pm.openFile(entry.path, O_RDONLY);
                if (result.has_value())
                    entry.openedFd = result.value();
            }
            if (entry.openedFd != -1)
                return entry.openedFd;
        }
        else if ((entry.type == Type::HereDoc || entry.type == Type::HereString)
                 && entry.targetFd == STDIN_FILENO)
        {
            // Lazily create the pipe if not already created
            if (entry.openedFd == -1)
            {
                auto pipeResult = createPipe();
                if (pipeResult.has_value())
                {
                    auto pipe = std::move(pipeResult.value());
                    // Write content to pipe
                    write(pipe->writer(), entry.content.data(), entry.content.size());
                    // Add trailing newline for herestrings if needed
                    if (entry.type == Type::HereString && !entry.content.empty()
                        && entry.content.back() != '\n')
                    {
                        write(pipe->writer(), "\n", 1);
                    }
                    pipe->closeWriter();
                    entry.openedFd = pipe->releaseReader();
                }
            }
            if (entry.openedFd != -1)
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
    if (savedStdout != -1)
    {
        savedStdout = -1;
    }
    output.clear();
}

// ========================================================================
// Shell implementation
// ========================================================================

Shell::Shell(): Shell(RealTTY::instance(), SystemEnvironment::instance())
{
}

Shell::Shell(TTY& tty, Environment& env):
    _env { env }, _tty { tty }, _processManager { PosixProcessManager::instance() }
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
    if (auto const* home = std::getenv("HOME"))
        _outputDefinitions.loadFromDirectory(std::filesystem::path(home) / ".config" / "endo"
                                             / "definitions");

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

    // Initialize completion system
    completer = std::make_unique<Completer>(_env, history, _fsharpState);
    prompt.setCompleter(completer.get());

    // NB: These lines could go away once we have a proper command line parser and
    //     the ability to set these options from the command line.
    registerBuiltinFunctions();
}

Shell::~Shell()
{
    SignalHandler::restore();
}

Environment& Shell::environment() noexcept
{
    return _env;
}

Environment const& Shell::environment() const noexcept
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

    auto const cwd = _env.get("PWD").value_or(std::filesystem::current_path().string());

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
    if (gethostname(hostname.data(), hostname.size()) != 0)
        hostname[0] = '\0';

    _tty.writeToStdout(std::format("\033]7;file://{}{}\033\\", hostname.data(), encoded));
}

int Shell::run()
{
    if (_interactive && !_tty.isTerminal())
    {
        std::cerr << "endo: interactive mode requires a terminal.\n";
        return EXIT_FAILURE;
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
            _exitCode = execute(lineBuffer);
            emitCommandFinished(_exitCode);

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
    // Windows fallback: simple loop without poll
    while (!_quit && prompt.ready())
    {
        emitCurrentWorkingDirectory();
        emitPromptStart();

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
        _exitCode = execute(lineBuffer);
        emitCommandFinished(_exitCode);

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
        if (!_fsharpState.functions.empty() || !_fsharpState.valueBindings.empty())
        {
            std::unordered_set<std::string> names;
            for (auto const& [name, _]: _fsharpState.functions)
                names.insert(name);
            for (auto const& binding: _fsharpState.valueBindings)
                names.insert(binding.name);
            parser.setKnownFSharpFunctions(std::move(names));
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

        if (irLog().is_enabled())
        {
            irLog()()("================================================\n");
            irLog()()("Linked target code (bytecode):\n");
            irLog()()("{}", _currentProgram->dumpToString());
        }

        CoreVM::Handler* main = _currentProgram->findHandler("@main");
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

void Shell::registerBuiltinFunctions()
{
    // clang-format off
    _runtime.registerFunction("exit")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExit, this);

    _runtime.registerFunction("export")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExport, this);

    _runtime.registerFunction("export")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSetAndExport, this);

    _runtime.registerFunction("cd")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinChDirHome, this);

    _runtime.registerFunction("cd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinChDir, this);

    _runtime.registerFunction("set")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinSet, this);

    _runtime.registerFunction("unset")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinUnset, this);

    _runtime.registerFunction("getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetVar, this);

    // F#-style env builtin: env.has checks existence, env.get retrieves the value
    _runtime.registerFunction("env.has")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            args.setResult(_env.get(key).has_value());
        });

    _runtime.registerFunction("env.get")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::String)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            auto val = _env.get(key);
            args.setResult(std::string(val.value_or("")));
        });

    _runtime.registerFunction("getvar.exitstatus")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetExitStatus, this);

    _runtime.registerFunction("setvar.exitstatus")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSetExitStatus, this);

    _runtime.registerFunction("getvar.processid")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetProcessId, this);

    _runtime.registerFunction("getvar.backgroundid")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetBackgroundId, this);

    _runtime.registerFunction("getvar.positional")
        .param<CoreVM::CoreNumber>("index")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetPositional, this);

    _runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcess, this);

    _runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcessShellPiped, this);

    _runtime.registerFunction("read")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinReadDefault, this);

    _runtime.registerFunction("read")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinRead, this);

    _runtime.registerFunction("internal.open_read")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenRead, this);

    _runtime.registerFunction("internal.open_write")
        .param<std::string>("path")
        .param<CoreVM::CoreNumber>("oflags")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenWrite, this);

    _runtime.registerFunction("internal.cmd_start")
        .param<std::string>("program")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinCmdStart, this);

    _runtime.registerFunction("internal.cmd_arg")
        .param<std::string>("arg")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinCmdArg, this);

    _runtime.registerFunction("internal.cmd_exec")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExec, this);

    _runtime.registerFunction("internal.cmd_exec_piped")
        .param<bool>("last_in_chain")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExecPiped, this);

    _runtime.registerFunction("internal.redirect_start")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectStart, this);

    _runtime.registerFunction("internal.redirect_input")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectInput, this);

    _runtime.registerFunction("internal.redirect_output")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<std::string>("path")
        .param<bool>("append")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectOutput, this);

    _runtime.registerFunction("internal.redirect_fd_dup")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<CoreVM::CoreNumber>("target_fd")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectFdDup, this);

    _runtime.registerFunction("internal.redirect_heredoc")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectHeredoc, this);

    _runtime.registerFunction("internal.redirect_herestring")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectHerestring, this);

    _runtime.registerFunction("internal.redirect_end")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectEnd, this);

    _runtime.registerFunction("internal.subst_start")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSubstStart, this);

    _runtime.registerFunction("internal.subst_end")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinSubstEnd, this);

    _runtime.registerFunction("internal.procsubst_fork")
        .param<bool>("is_write")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinProcSubstFork, this);

    _runtime.registerFunction("internal.procsubst_exit")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinProcSubstExit, this);

    _runtime.registerFunction("internal.procsubst_get_path")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinProcSubstGetPath, this);

    _runtime.registerFunction("internal.procsubst_cleanup")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinProcSubstCleanup, this);

    _runtime.registerFunction("expand.tilde")
        .param<std::string>("suffix")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandTilde, this);

    _runtime.registerFunction("expand.tilde_user")
        .param<std::string>("user")
        .param<std::string>("suffix")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandTildeUser, this);

    _runtime.registerFunction("expand.glob")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExpandGlob, this);

    _runtime.registerFunction("expand.arith_to_string")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinArithToString, this);

    _runtime.registerFunction("expand.arith_getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinArithGetVar, this);

    _runtime.registerFunction("expand.arith_pow")
        .param<CoreVM::CoreNumber>("base")
        .param<CoreVM::CoreNumber>("exp")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinArithPow, this);

    _runtime.registerFunction("expand.param_length")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamLength, this);

    _runtime.registerFunction("expand.param_default")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamDefault, this);

    _runtime.registerFunction("expand.param_alternate")
        .param<std::string>("var")
        .param<std::string>("alternate")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamAlternate, this);

    _runtime.registerFunction("expand.param_assign")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamAssign, this);

    _runtime.registerFunction("expand.param_error")
        .param<std::string>("var")
        .param<std::string>("error_msg")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamError, this);

    _runtime.registerFunction("expand.param_remove_prefix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamRemovePrefix, this);

    _runtime.registerFunction("expand.param_remove_suffix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamRemoveSuffix, this);

    _runtime.registerFunction("expand.param_replace")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<std::string>("replacement")
        .param<bool>("all")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamReplace, this);

    _runtime.registerFunction("internal.for_init")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForInit, this);

    _runtime.registerFunction("internal.for_add_item")
        .param<std::string>("item")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForAddItem, this);

    _runtime.registerFunction("internal.for_has_more")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinForHasMore, this);

    _runtime.registerFunction("internal.for_next")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForNext, this);

    _runtime.registerFunction("internal.for_cleanup")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForCleanup, this);

    _runtime.registerFunction("internal.case_match")
        .param<std::string>("word")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinCaseMatch, this);

    _runtime.registerFunction("internal.function_register")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinFunctionRegister, this);

    _runtime.registerFunction("internal.function_call")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFunctionCall, this);

    // Job control builtins
    _runtime.registerFunction("jobs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinJobs, this);

    _runtime.registerFunction("fg")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFg, this);

    _runtime.registerFunction("fg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFg, this);

    _runtime.registerFunction("bg")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBg, this);

    _runtime.registerFunction("bg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBg, this);

    _runtime.registerFunction("wait")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWait, this);

    _runtime.registerFunction("wait")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWait, this);

    _runtime.registerFunction("internal.cmd_exec_piped_background")
        .param<std::string>("command")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExecPipedBackground, this);

    // Keybinding management
    // bind                 - list all bindings
    // bind <key> <action>  - bind a key to an action
    // bind -r <key>        - remove a binding
    // bind --reset         - reset to defaults
    _runtime.registerFunction("bind")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBind, this);

    _runtime.registerFunction("bind")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBind, this);

    // which                        - show help
    // which <program>...           - find program(s) in PATH
    // which -a <program>...        - show all matches
    // which -h/--help              - show help
    // which -i/--read-alias        - also show aliases (not yet implemented)
    _runtime.registerFunction("which")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWhich, this);

    _runtime.registerFunction("which")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWhich, this);

    // F# print builtins
    _runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinPrint, this);

    _runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinPrintln, this);

    // Bare expression display: auto-display a value with table rendering for lists of records
    _runtime.registerFunction("display_result")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinDisplayResult, this);

    // Helper: converts a list TypedObject to string "[1; 2; 3]" (delegates to valueToString)
    auto listToString = [](CoreVM::TypedObject* obj, CoreVM::Runner* runner) -> std::string {
        return valueToString(reinterpret_cast<uintptr_t>(obj), runner);
    };

    // F# list_to_string builtin: converts list object to "[1; 2; 3]" string
    _runtime.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([listToString](CoreVM::Params& args) {
            auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(args.caller()->newString(listToString(obj, args.caller())));
        });

    // F# list_concat builtin: concatenates two lists
    _runtime.registerFunction("list_concat")
        .param<CoreVM::CoreNumber>("left")
        .param<CoreVM::CoreNumber>("right")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* left = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            auto* right = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

            if (!left || left->tag == 0)
            {
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(right)));
                return;
            }

            std::vector<uint64_t> elements;
            auto* cur = left;
            while (cur && cur->tag == 1)
            {
                elements.push_back(cur->getSlot(0));
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }

            CoreVM::TypedObject* acc = right;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                auto* cons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1;
                cons->setSlot(0, *it);
                cons->setSlot(1, reinterpret_cast<uintptr_t>(acc));
                acc = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // F# list_head builtin: returns Option (Some head | None)
    _runtime.registerFunction("list_head")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (!list || list->tag == 0)
            {
                auto* none = args.caller()->allocObject(CoreVM::BuiltinTypeId::Option);
                none->tag = 0;
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
            }
            else
            {
                auto* some = args.caller()->allocObject(CoreVM::BuiltinTypeId::Option);
                some->tag = 1;
                some->setSlot(0, list->getSlot(0));
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
            }
        });

    // F# list_tail builtin: returns tail of list (or [] for empty)
    _runtime.registerFunction("list_tail")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (!list || list->tag == 0)
            {
                auto* nil = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                nil->tag = 0;
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
            }
            else
            {
                args.setResult(static_cast<CoreVM::CoreNumber>(list->getSlot(1)));
            }
        });

    // F# list_length builtin: returns number of elements
    _runtime.registerFunction("list_length")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            int64_t count = 0;
            while (cur && cur->tag == 1)
            {
                ++count;
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(count));
        });

    // F# list_isEmpty builtin: returns true if list is Nil
    _runtime.registerFunction("list_isEmpty")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(!list || list->tag == 0);
        });

    // F# list_sort builtin: sorts list elements numerically (ascending)
    _runtime.registerFunction("list_sort")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            std::vector<int64_t> elements;
            while (cur && cur->tag == 1)
            {
                elements.push_back(static_cast<int64_t>(cur->getSlot(0)));
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }
            std::ranges::sort(elements);
            auto* acc = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
            acc->tag = 0;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                auto* cons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1;
                cons->setSlot(0, static_cast<uint64_t>(*it));
                cons->setSlot(1, reinterpret_cast<uintptr_t>(acc));
                acc = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // F# list_distinct builtin: removes duplicate elements preserving first-seen order
    _runtime.registerFunction("list_distinct")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            std::vector<int64_t> elements;
            std::unordered_set<int64_t> seen;
            while (cur && cur->tag == 1)
            {
                auto val = static_cast<int64_t>(cur->getSlot(0));
                if (seen.insert(val).second)
                    elements.push_back(val);
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }
            auto* acc = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
            acc->tag = 0;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                auto* cons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1;
                cons->setSlot(0, static_cast<uint64_t>(*it));
                cons->setSlot(1, reinterpret_cast<uintptr_t>(acc));
                acc = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // F# list_sort_pairs builtin: sorts Tuple2(key, elem) pairs by key, returns elements
    _runtime.registerFunction("list_sort_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            std::vector<std::pair<int64_t, uint64_t>> pairs;
            while (cur && cur->tag == 1)
            {
                auto* tuple = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(0));
                auto key = static_cast<int64_t>(tuple->getSlot(0));
                auto elem = tuple->getSlot(1);
                pairs.emplace_back(key, elem);
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }
            std::ranges::reverse(pairs);
            std::ranges::stable_sort(pairs, {}, &std::pair<int64_t, uint64_t>::first);
            auto* acc = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
            acc->tag = 0;
            for (auto it = pairs.rbegin(); it != pairs.rend(); ++it)
            {
                auto* cons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1;
                cons->setSlot(0, it->second);
                cons->setSlot(1, reinterpret_cast<uintptr_t>(acc));
                acc = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // F# list_group_pairs builtin: groups Tuple2(key, elem) pairs by key
    _runtime.registerFunction("list_group_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            std::vector<std::pair<int64_t, uint64_t>> pairs;
            while (cur && cur->tag == 1)
            {
                auto* tuple = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(0));
                auto key = static_cast<int64_t>(tuple->getSlot(0));
                auto elem = tuple->getSlot(1);
                pairs.emplace_back(key, elem);
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }
            std::ranges::reverse(pairs);
            std::vector<int64_t> groupOrder;
            std::unordered_map<int64_t, std::vector<uint64_t>> groups;
            for (auto const& [key, elem] : pairs)
            {
                if (groups.find(key) == groups.end())
                    groupOrder.push_back(key);
                groups[key].push_back(elem);
            }
            auto* outerAcc = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
            outerAcc->tag = 0;
            for (auto it = groupOrder.rbegin(); it != groupOrder.rend(); ++it)
            {
                auto key = *it;
                auto const& elems = groups[key];
                auto* innerAcc = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                innerAcc->tag = 0;
                for (auto eit = elems.rbegin(); eit != elems.rend(); ++eit)
                {
                    auto* innerCons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                    innerCons->tag = 1;
                    innerCons->setSlot(0, *eit);
                    innerCons->setSlot(1, reinterpret_cast<uintptr_t>(innerAcc));
                    innerAcc = innerCons;
                }
                auto* tuple = args.caller()->allocObject(CoreVM::BuiltinTypeId::Tuple2);
                tuple->setSlot(0, static_cast<uint64_t>(key));
                tuple->setSlot(1, reinterpret_cast<uintptr_t>(innerAcc));
                auto* outerCons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                outerCons->tag = 1;
                outerCons->setSlot(0, reinterpret_cast<uintptr_t>(tuple));
                outerCons->setSlot(1, reinterpret_cast<uintptr_t>(outerAcc));
                outerAcc = outerCons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(outerAcc)));
        });

    // F# object_to_string builtin: runtime dispatch for object printing
    _runtime.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto rawVal = static_cast<uint64_t>(args.getInt(1));
            args.setResult(args.caller()->newString(valueToString(rawVal, args.caller())));
        });

    // F# string_repeat builtin: "ha" * 3 → "hahaha"
    _runtime.registerFunction("string_repeat")
        .param<CoreVM::CoreString>("str")
        .param<CoreVM::CoreNumber>("count")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const& str = args.getString(1);
            auto const count = args.getInt(2);
            std::string result;
            if (count > 0)
            {
                result.reserve(static_cast<size_t>(count) * str.size());
                for (int64_t i = 0; i < count; ++i)
                    result += str;
            }
            args.setResult(args.caller()->newString(result));
        });

    // F# string_trim builtin: removes leading/trailing whitespace
    _runtime.registerFunction("string_trim")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            auto const start = str.find_first_not_of(" \t\n\r");
            if (start == std::string::npos)
                args.setResult(args.caller()->newString(""));
            else
            {
                auto const end = str.find_last_not_of(" \t\n\r");
                args.setResult(args.caller()->newString(str.substr(start, end - start + 1)));
            }
        });

    // F# string_toLower builtin: converts string to lowercase
    _runtime.registerFunction("string_toLower")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            std::ranges::transform(str, str.begin(), ::tolower);
            args.setResult(args.caller()->newString(str));
        });

    // F# string_toUpper builtin: converts string to uppercase
    _runtime.registerFunction("string_toUpper")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            std::ranges::transform(str, str.begin(), ::toupper);
            args.setResult(args.caller()->newString(str));
        });

    // F# string_contains builtin: checks if haystack contains needle
    _runtime.registerFunction("string_contains")
        .param<CoreVM::CoreString>("haystack")
        .param<CoreVM::CoreString>("needle")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            args.setResult(args.getString(1).find(args.getString(2)) != std::string_view::npos);
        });

    // F# string_startsWith builtin: checks if text starts with prefix
    _runtime.registerFunction("string_startsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("prefix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            args.setResult(args.getString(1).starts_with(args.getString(2)));
        });

    // F# string_endsWith builtin: checks if text ends with suffix
    _runtime.registerFunction("string_endsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            args.setResult(args.getString(1).ends_with(args.getString(2)));
        });

    // F# string_replace builtin: replaces all occurrences of old with new in text
    _runtime.registerFunction("string_replace")
        .param<CoreVM::CoreString>("old_str")
        .param<CoreVM::CoreString>("new_str")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto text = std::string(args.getString(3));
            auto const old_s = args.getString(1);
            auto const new_s = args.getString(2);
            if (!old_s.empty())
            {
                size_t pos = 0;
                while ((pos = text.find(old_s, pos)) != std::string::npos)
                {
                    text.replace(pos, old_s.size(), new_s);
                    pos += new_s.size();
                }
            }
            args.setResult(args.caller()->newString(text));
        });

    // F# string_split builtin: splits text by delimiter into list<str>
    _runtime.registerFunction("string_split")
        .param<CoreVM::CoreString>("delimiter")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([](CoreVM::Params& args) {
            auto const text = std::string(args.getString(2));
            auto const delim = std::string(args.getString(1));
            auto* runner = args.caller();

            std::vector<std::string> parts;
            if (delim.empty())
            {
                for (auto c : text)
                    parts.emplace_back(1, c);
            }
            else
            {
                size_t pos = 0;
                size_t found = 0;
                while ((found = text.find(delim, pos)) != std::string::npos)
                {
                    parts.push_back(text.substr(pos, found - pos));
                    pos = found + delim.size();
                }
                parts.push_back(text.substr(pos));
            }

            // Build cons-cell list right-to-left
            auto* list = runner->allocObject(CoreVM::BuiltinTypeId::List);
            list->tag = 0; // Nil
            for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            {
                auto* cons = runner->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1; // Cons
                cons->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(*it)));
                cons->setSlot(1, reinterpret_cast<uintptr_t>(list));
                list = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // F# string_join builtin: joins list<str> with separator
    _runtime.registerFunction("string_join")
        .param<CoreVM::CoreString>("separator")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const sep = std::string(args.getString(1));
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

            std::string result;
            bool first = true;
            while (list && list->tag == 1)
            {
                if (!first)
                    result += sep;
                auto* str = reinterpret_cast<CoreVM::CoreString*>(list->getSlot(0));
                if (str)
                    result += *str;
                list = reinterpret_cast<CoreVM::TypedObject*>(list->getSlot(1));
                first = false;
            }
            args.setResult(args.caller()->newString(result));
        });

    // F# structured_ps builtin: returns list<ProcessInfo> from platform process provider
    _runtime.registerFunction("structured_ps")
        .returnType(CoreVM::LiteralType::Number)  // Returns list object pointer
        .bind([this](CoreVM::Params& args) {
            LinuxProcessProvider provider;
            PsCommand cmd(provider);
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // F# structured_ls builtin: returns list<FileInfo> from platform file info provider
    _runtime.registerFunction("structured_ls")
        .param<CoreVM::CoreString>("path")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([this](CoreVM::Params& args) {
            auto const path = args.getString(1);
            LinuxFileInfoProvider provider;
            LsCommand cmd(provider, std::string(path));
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // F# structured_jobs builtin: returns list<JobInfo> from the shell's job table
    _runtime.registerFunction("structured_jobs")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([this](CoreVM::Params& args) {
            // Bridge JobTable to JobProvider interface inline
            class ShellJobProvider final: public JobProvider
            {
              public:
                explicit ShellJobProvider(JobTable const& table): _table(table) {}
                [[nodiscard]] std::vector<JobEntry> listJobs() const override
                {
                    std::vector<JobEntry> result;
                    for (auto const* job: _table.listJobs())
                    {
                        result.push_back(JobEntry {
                            .id = job->id,
                            .state = std::string(toString(job->state)),
                            .command = job->command,
                            .pid = static_cast<int64_t>(job->pgid),
                        });
                    }
                    return result;
                }

              private:
                JobTable const& _table;
            };

            ShellJobProvider provider(jobTable);
            JobsCommand cmd(provider);
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // Helper builtins for FileInfo mode/mtime formatting and testing
    _runtime.registerFunction("format_datetime")
        .param<CoreVM::CoreNumber>("epoch")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const epoch = static_cast<time_t>(args.getInt(1));
            struct tm tm {};
            gmtime_r(&epoch, &tm);
            auto result = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                      tm.tm_hour, tm.tm_min, tm.tm_sec);
            args.setResult(args.caller()->newString(result));
        });

    _runtime.registerFunction("format_mode")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            std::string result;
            result += (mode & 0400) ? 'r' : '-';
            result += (mode & 0200) ? 'w' : '-';
            result += (mode & 0100) ? 'x' : '-';
            result += (mode & 0040) ? 'r' : '-';
            result += (mode & 0020) ? 'w' : '-';
            result += (mode & 0010) ? 'x' : '-';
            result += (mode & 0004) ? 'r' : '-';
            result += (mode & 0002) ? 'w' : '-';
            result += (mode & 0001) ? 'x' : '-';
            args.setResult(args.caller()->newString(result));
        });

    _runtime.registerFunction("mode_isReadable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0444) != 0 ? 1 : 0));
        });

    _runtime.registerFunction("mode_isWritable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0222) != 0 ? 1 : 0));
        });

    _runtime.registerFunction("mode_isExecutable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0111) != 0 ? 1 : 0));
        });

    // Register structured command callbacks for output definitions
    for (auto const& def: _outputDefinitions.definitions())
    {
        for (auto const& variant: def.variants)
        {
            auto const callbackName = "structured_" + variant.fsharpName;
            auto const* variantPtr = &variant;
            auto const command = def.command;

            _runtime.registerFunction(callbackName)
                .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
                .bind([this, variantPtr, command](CoreVM::Params& args) {
                    // Build the command to run
                    auto const& cmd = variantPtr->commandToRun.value_or(command);

                    // Execute the command and capture stdout via pipe
                    auto pipeResult = createPipe();
                    if (!pipeResult.has_value())
                    {
                        auto* nil = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                        nil->tag = 0;
                        args.setResult(
                            static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
                        return;
                    }
                    auto& pipe = pipeResult.value();

                    SpawnConfig config;
                    config.program = "/bin/sh";
                    config.arguments = { "sh", "-c", cmd };
                    config.stdinFd = _tty.inputFd();
                    config.stdoutFd = pipe->writer();
                    config.stderrFd = 2; // Keep stderr

                    auto pidResult = _processManager.spawn(config);
                    pipe->closeWriter();

                    // Read all output
                    std::string output;
                    char buf[4096];
                    while (true)
                    {
                        auto const n = ::read(pipe->reader(), buf, sizeof(buf));
                        if (n <= 0)
                            break;
                        output.append(buf, static_cast<size_t>(n));
                    }
                    pipe->closeReader();

                    if (pidResult.has_value())
                        (void) _processManager.wait(*pidResult);

                    // Parse the output into structured records
                    CoreVM::TypedObject* result = nullptr;
                    if (variantPtr->parser.type == ParserConfig::Type::Json)
                        result = OutputParser::parseJson(*args.caller(), output, *variantPtr);
                    else
                        result = OutputParser::parseFields(*args.caller(), output, *variantPtr);

                    args.setResult(
                        static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
                });
        }
    }

    // --- Data source wrappers (open-json, open-csv, from-json, from-csv) ---

    _runtime.registerFunction("open_json")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& filePath = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Read file contents
            std::string contents;
            try
            {
                auto const path = std::filesystem::path(filePath);
                if (std::filesystem::exists(path))
                {
                    auto ifs = std::ifstream(path);
                    contents.assign(std::istreambuf_iterator<char>(ifs),
                                    std::istreambuf_iterator<char>());
                }
            }
            catch (...)
            {
            }

            // Build variant from schema descriptor
            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Json);

            // Auto-detect JSON format: first non-whitespace '[' → Array, else → Lines
            auto const firstNonWs = contents.find_first_not_of(" \t\r\n");
            if (firstNonWs != std::string::npos && contents[firstNonWs] == '[')
                variant.parser.format = ParserConfig::Format::Array;
            else
                variant.parser.format = ParserConfig::Format::Lines;

            auto* result = OutputParser::parseJson(*args.caller(), contents, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    _runtime.registerFunction("open_csv")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& filePath = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Read file contents
            std::string contents;
            try
            {
                auto const path = std::filesystem::path(filePath);
                if (std::filesystem::exists(path))
                {
                    auto ifs = std::ifstream(path);
                    contents.assign(std::istreambuf_iterator<char>(ifs),
                                    std::istreambuf_iterator<char>());
                }
            }
            catch (...)
            {
            }

            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Fields);

            // Auto-detect CSV header: check if first line matches schema field names
            if (!contents.empty())
            {
                auto const firstNewline = contents.find('\n');
                auto const firstLine = contents.substr(0, firstNewline);
                if (OutputParser::detectCsvHeader(firstLine, variant.parser.fieldSeparator, variant.schema))
                {
                    // Skip the header line
                    if (firstNewline != std::string::npos)
                        contents = contents.substr(firstNewline + 1);
                    else
                        contents.clear();
                }
            }

            auto* result = OutputParser::parseFields(*args.caller(), contents, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    _runtime.registerFunction("from_json")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& sourceCmd = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Spawn source command and capture output
            std::string output;
            if (!sourceCmd.empty())
            {
                auto pipeResult = createPipe();
                if (pipeResult.has_value())
                {
                    auto& pipe = pipeResult.value();
                    SpawnConfig config;
                    config.program = "/bin/sh";
                    config.arguments = { "sh", "-c", std::string(sourceCmd) };
                    config.stdinFd = _tty.inputFd();
                    config.stdoutFd = pipe->writer();
                    config.stderrFd = 2;

                    auto pidResult = _processManager.spawn(config);
                    pipe->closeWriter();

                    char buf[4096];
                    while (true)
                    {
                        auto const n = ::read(pipe->reader(), buf, sizeof(buf));
                        if (n <= 0)
                            break;
                        output.append(buf, static_cast<size_t>(n));
                    }
                    pipe->closeReader();
                    if (pidResult.has_value())
                        (void) _processManager.wait(*pidResult);
                }
            }

            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Json);

            auto const firstNonWs = output.find_first_not_of(" \t\r\n");
            if (firstNonWs != std::string::npos && output[firstNonWs] == '[')
                variant.parser.format = ParserConfig::Format::Array;
            else
                variant.parser.format = ParserConfig::Format::Lines;

            auto* result = OutputParser::parseJson(*args.caller(), output, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    _runtime.registerFunction("from_csv")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& sourceCmd = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Spawn source command and capture output
            std::string output;
            if (!sourceCmd.empty())
            {
                auto pipeResult = createPipe();
                if (pipeResult.has_value())
                {
                    auto& pipe = pipeResult.value();
                    SpawnConfig config;
                    config.program = "/bin/sh";
                    config.arguments = { "sh", "-c", std::string(sourceCmd) };
                    config.stdinFd = _tty.inputFd();
                    config.stdoutFd = pipe->writer();
                    config.stderrFd = 2;

                    auto pidResult = _processManager.spawn(config);
                    pipe->closeWriter();

                    char buf[4096];
                    while (true)
                    {
                        auto const n = ::read(pipe->reader(), buf, sizeof(buf));
                        if (n <= 0)
                            break;
                        output.append(buf, static_cast<size_t>(n));
                    }
                    pipe->closeReader();
                    if (pidResult.has_value())
                        (void) _processManager.wait(*pidResult);
                }
            }

            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Fields);

            // Auto-detect CSV header
            if (!output.empty())
            {
                auto const firstNewline = output.find('\n');
                auto const firstLine = output.substr(0, firstNewline);
                if (OutputParser::detectCsvHeader(firstLine, variant.parser.fieldSeparator, variant.schema))
                {
                    if (firstNewline != std::string::npos)
                        output = output.substr(firstNewline + 1);
                    else
                        output.clear();
                }
            }

            auto* result = OutputParser::parseFields(*args.caller(), output, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // clang-format on
}

// ========================================================================
// Builtin implementations
// ========================================================================

void Shell::builtinExit(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
    _runner->suspend();
    _quit = true;
}

void Shell::builtinCallProcess(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);
    std::string const& program = args.at(0);

    // Handle echo builtin
    if (program == "echo")
    {
        std::vector<std::string> echoArgs;
        for (size_t i = 1; i < args.size(); ++i)
            echoArgs.push_back(args.at(i));

        bool suppressNewline = false;
        bool interpretEscapes = false;
        size_t argStart = 0;

        // Parse flags
        for (size_t i = 0; i < echoArgs.size(); ++i)
        {
            std::string_view arg = echoArgs[i];

            if (arg == "--")
            {
                argStart = i + 1;
                break;
            }

            if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
            {
                bool validFlag = true;
                for (size_t j = 1; j < arg.size(); ++j)
                {
                    if (arg[j] == 'n')
                        suppressNewline = true;
                    else if (arg[j] == 'e')
                        interpretEscapes = true;
                    else
                    {
                        validFlag = false;
                        break;
                    }
                }

                if (validFlag)
                {
                    argStart = i + 1;
                    continue;
                }
            }

            argStart = i;
            break;
        }

        // Build output string
        std::string output;
        for (size_t i = argStart; i < echoArgs.size(); ++i)
        {
            if (i > argStart)
                output += ' ';
            output += echoArgs[i];
        }

        // Process escape sequences if -e flag is set
        if (interpretEscapes)
            output = processEscapeSequences(output);

        // Add newline if not suppressed
        if (!suppressNewline)
            output += '\n';

        // Get the effective stdout fd considering redirects
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);

        // Write to the correct output fd (respects redirects and test environments)
        [[maybe_unused]] auto written = write(outputFd, output.data(), output.size());

        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Handle cat builtin
    if (program == "cat")
    {
        std::vector<std::string> catArgs;
        for (size_t i = 1; i < args.size(); ++i)
            catArgs.push_back(args.at(i));

        // Parse flags
        bool numberLines = false;
        bool numberNonBlank = false;
        bool squeezeBlank = false;
        bool showEnds = false;
        bool showTabs = false;
        bool showHelp = false;
        std::vector<std::string> files;

        for (size_t i = 0; i < catArgs.size(); ++i)
        {
            std::string_view arg = catArgs[i];

            if (arg == "--")
            {
                // Everything after -- is a file
                for (size_t j = i + 1; j < catArgs.size(); ++j)
                    files.push_back(catArgs[j]);
                break;
            }

            if (arg == "--help")
            {
                showHelp = true;
                continue;
            }
            if (arg == "--number")
            {
                numberLines = true;
                continue;
            }
            if (arg == "--number-nonblank")
            {
                numberNonBlank = true;
                continue;
            }
            if (arg == "--squeeze-blank")
            {
                squeezeBlank = true;
                continue;
            }
            if (arg == "--show-ends")
            {
                showEnds = true;
                continue;
            }
            if (arg == "--show-tabs")
            {
                showTabs = true;
                continue;
            }
            if (arg == "--show-all")
            {
                showEnds = true;
                showTabs = true;
                continue;
            }

            if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
            {
                bool validFlag = true;
                for (size_t j = 1; j < arg.size(); ++j)
                {
                    switch (arg[j])
                    {
                        case 'n': numberLines = true; break;
                        case 'b': numberNonBlank = true; break;
                        case 's': squeezeBlank = true; break;
                        case 'E': showEnds = true; break;
                        case 'T': showTabs = true; break;
                        case 'A':
                            showEnds = true;
                            showTabs = true;
                            break;
                        case 'h': showHelp = true; break;
                        default: validFlag = false; break;
                    }
                    if (!validFlag)
                        break;
                }
                if (validFlag)
                    continue;
            }

            // Not a flag, treat as file
            files.push_back(std::string(arg));
        }

        // -b overrides -n
        if (numberNonBlank)
            numberLines = false;

        // Helper to write output
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);

        auto writeOutput = [outputFd](std::string const& str) {
            [[maybe_unused]] auto written = write(outputFd, str.data(), str.size());
        };

        if (showHelp)
        {
            writeOutput("Usage: cat [OPTION]... [FILE]...\n");
            writeOutput("Concatenate FILE(s) to standard output.\n");
            writeOutput("With no FILE, or when FILE is -, read standard input.\n");
            writeOutput("\n");
            writeOutput("  -n, --number           number all output lines\n");
            writeOutput("  -b, --number-nonblank  number non-blank output lines (overrides -n)\n");
            writeOutput("  -s, --squeeze-blank    suppress repeated empty output lines\n");
            writeOutput("  -E, --show-ends        display $ at end of each line\n");
            writeOutput("  -T, --show-tabs        display TAB characters as ^I\n");
            writeOutput("  -A, --show-all         equivalent to -ET\n");
            writeOutput("  -h, --help             display this help and exit\n");
            _exitCode = 0;
            context.setResult(CoreVM::CoreNumber(0));
            return;
        }

        // Helper to process and output content
        int lineNumber = 1;
        bool lastLineWasBlank = false;

        auto processContent = [&](std::string const& content) {
            std::string line;
            for (size_t i = 0; i < content.size(); ++i)
            {
                char c = content[i];
                if (c == '\n')
                {
                    bool isBlank = line.empty();

                    // Squeeze blank lines
                    if (squeezeBlank && isBlank && lastLineWasBlank)
                    {
                        line.clear();
                        continue;
                    }
                    lastLineWasBlank = isBlank;

                    // Process tabs
                    if (showTabs)
                    {
                        std::string processed;
                        for (char ch: line)
                        {
                            if (ch == '\t')
                                processed += "^I";
                            else
                                processed += ch;
                        }
                        line = std::move(processed);
                    }

                    // Build output line
                    std::string output;
                    if (numberNonBlank && !isBlank)
                    {
                        output = std::format("{:>6}\t", lineNumber++);
                    }
                    else if (numberLines)
                    {
                        output = std::format("{:>6}\t", lineNumber++);
                    }
                    output += line;
                    if (showEnds)
                        output += '$';
                    output += '\n';
                    writeOutput(output);
                    line.clear();
                }
                else
                {
                    line += c;
                }
            }
            // Handle last line without newline
            if (!line.empty())
            {
                if (showTabs)
                {
                    std::string processed;
                    for (char ch: line)
                    {
                        if (ch == '\t')
                            processed += "^I";
                        else
                            processed += ch;
                    }
                    line = std::move(processed);
                }

                std::string output;
                if (numberNonBlank && !line.empty())
                {
                    output = std::format("{:>6}\t", lineNumber++);
                }
                else if (numberLines)
                {
                    output = std::format("{:>6}\t", lineNumber++);
                }
                output += line;
                writeOutput(output);
            }
        };

        // Helper to read from fd
        auto readFromFd = [](NativeHandle fd) -> std::string {
            std::string content;
            char buffer[4096];
            ssize_t bytesRead;
            while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
                content.append(buffer, static_cast<size_t>(bytesRead));
            return content;
        };

        bool success = true;

        // If no files specified, read from stdin
        if (files.empty())
            files.push_back("-");

        for (auto const& file: files)
        {
            if (file == "-")
            {
                // Read from stdin
                NativeHandle const inputFd = _redirectState.getEffectiveStdinFd(
                    _currentPipelineBuilder.defaultStdinFd, _processManager);
                std::string content = readFromFd(inputFd);
                processContent(content);
            }
            else
            {
                // Read from file
                auto const result = _processManager.openFile(file, O_RDONLY);
                if (!result.has_value())
                {
                    error("cat: {}: {}", file, strerror(errno));
                    success = false;
                    continue;
                }
                NativeHandle fd = result.value();
                std::string content = readFromFd(fd);
                close(fd);
                processContent(content);
            }
        }

        _exitCode = success ? 0 : 1;
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    // Handle sleep builtin
    if (program == "sleep")
    {
        std::vector<std::string> sleepArgs;
        for (size_t i = 1; i < args.size(); ++i)
            sleepArgs.push_back(args.at(i));

        // Helper to write output
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);

        auto writeOutput = [outputFd](std::string const& str) {
            [[maybe_unused]] auto written = write(outputFd, str.data(), str.size());
        };

        // Check for help
        for (auto const& arg: sleepArgs)
        {
            if (arg == "-h" || arg == "--help")
            {
                writeOutput("Usage: sleep NUMBER[SUFFIX]...\n");
                writeOutput("Pause for NUMBER seconds.\n");
                writeOutput("\n");
                writeOutput("SUFFIX may be:\n");
                writeOutput("  s   seconds (default)\n");
                writeOutput("  m   minutes\n");
                writeOutput("  h   hours\n");
                writeOutput("  d   days\n");
                writeOutput("\n");
                writeOutput("Multiple arguments are summed together.\n");
                writeOutput("NUMBER may be an integer or floating-point number.\n");
                _exitCode = 0;
                context.setResult(CoreVM::CoreNumber(0));
                return;
            }
        }

        // No arguments - error
        if (sleepArgs.empty())
        {
            error("sleep: missing operand");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        // Parse duration arguments
        double totalSeconds = 0.0;
        for (auto const& arg: sleepArgs)
        {
            if (arg.empty())
                continue;

            double multiplier = 1.0;
            std::string numStr = arg;

            // Check for suffix
            char lastChar = arg.back();
            if (lastChar == 's' || lastChar == 'S')
            {
                multiplier = 1.0;
                numStr = arg.substr(0, arg.size() - 1);
            }
            else if (lastChar == 'm' || lastChar == 'M')
            {
                multiplier = 60.0;
                numStr = arg.substr(0, arg.size() - 1);
            }
            else if (lastChar == 'h' || lastChar == 'H')
            {
                multiplier = 3600.0;
                numStr = arg.substr(0, arg.size() - 1);
            }
            else if (lastChar == 'd' || lastChar == 'D')
            {
                multiplier = 86400.0;
                numStr = arg.substr(0, arg.size() - 1);
            }

            if (numStr.empty())
            {
                error("sleep: invalid time interval '{}'", arg);
                _exitCode = 1;
                context.setResult(CoreVM::CoreNumber(1));
                return;
            }

            // Parse number
            try
            {
                size_t pos = 0;
                double value = std::stod(numStr, &pos);
                if (pos != numStr.size())
                {
                    error("sleep: invalid time interval '{}'", arg);
                    _exitCode = 1;
                    context.setResult(CoreVM::CoreNumber(1));
                    return;
                }
                if (value < 0)
                {
                    error("sleep: invalid time interval '{}'", arg);
                    _exitCode = 1;
                    context.setResult(CoreVM::CoreNumber(1));
                    return;
                }
                totalSeconds += value * multiplier;
            }
            catch (std::exception const&)
            {
                error("sleep: invalid time interval '{}'", arg);
                _exitCode = 1;
                context.setResult(CoreVM::CoreNumber(1));
                return;
            }
        }

        // Sleep
        if (totalSeconds > 0)
        {
            auto const duration = std::chrono::duration<double>(totalSeconds);
            std::this_thread::sleep_for(duration);
        }

        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Check if this is a registered shell function
    if (_registeredFunctions.contains(program))
    {
        CoreVM::Handler* handler = _currentProgram->findHandler(program);
        if (!handler)
        {
            error("{}: function not found (was it defined in a previous command?)", program);
            _exitCode = 127;
            context.setResult(CoreVM::CoreNumber(127));
            return;
        }

        auto savedPositionalParams = _positionalParameters;
        _positionalParameters.clear();
        _positionalParameters.push_back(program);
        for (size_t i = 1; i < args.size(); ++i)
            _positionalParameters.push_back(args.at(i));

        auto runner = CoreVM::Runner(handler,
                                     nullptr,
                                     &_globals,
                                     CoreVM::RuntimeConfig::defaultConfig(),
                                     std::bind(&Shell::trace, this, _1, _2, _3));
        runner.run();

        _positionalParameters = std::move(savedPositionalParams);

        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(args.begin() + 1, args.end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    // Build command string for job table
    std::string command;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            command += ' ';
        command += args.at(i);
    }

    auto const fgResult = runForeground(config, command);
    if (!fgResult.has_value())
    {
        error("Failed to run {}: {}", program, toString(fgResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    _exitCode = fgResult->exitCode;
    if (fgResult->stopped)
        debugLog()()("child process stopped with signal {}\n", fgResult->exitCode);
    else
        debugLog()()("child process exited with code {}\n", _exitCode);

    cleanupProcSubst();

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinCallProcessShellPiped(CoreVM::Params& context)
{
    bool const lastInChain = context.getBool(1);
    CoreVM::CoreStringArray const& args = context.getStringArray(2);

    std::string const& program = args.at(0);

    // Handle echo builtin in pipeline
    if (program == "echo")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

        std::vector<std::string> echoArgs;
        for (size_t i = 1; i < args.size(); ++i)
            echoArgs.push_back(args.at(i));

        bool suppressNewline = false;
        bool interpretEscapes = false;
        size_t argStart = 0;

        // Parse flags
        for (size_t i = 0; i < echoArgs.size(); ++i)
        {
            std::string_view arg = echoArgs[i];

            if (arg == "--")
            {
                argStart = i + 1;
                break;
            }

            if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
            {
                bool validFlag = true;
                for (size_t j = 1; j < arg.size(); ++j)
                {
                    if (arg[j] == 'n')
                        suppressNewline = true;
                    else if (arg[j] == 'e')
                        interpretEscapes = true;
                    else
                    {
                        validFlag = false;
                        break;
                    }
                }

                if (validFlag)
                {
                    argStart = i + 1;
                    continue;
                }
            }

            argStart = i;
            break;
        }

        // Build output string
        std::string output;
        for (size_t i = argStart; i < echoArgs.size(); ++i)
        {
            if (i > argStart)
                output += ' ';
            output += echoArgs[i];
        }

        // Process escape sequences if -e flag is set
        if (interpretEscapes)
            output = processEscapeSequences(output);

        // Add newline if not suppressed
        if (!suppressNewline)
            output += '\n';

        // Write to the pipeline's stdout
        [[maybe_unused]] auto written = write(stdoutFd, output.data(), output.size());

        // Close write end of pipe so downstream can see EOF
        // Use the PipelineBuilder method to properly manage the Pipe object
        if (!lastInChain)
            _currentPipelineBuilder.closeCurrentPipeWriter();

        // Track command for job table
        std::string cmdString = "echo";
        for (size_t i = 1; i < args.size(); ++i)
        {
            cmdString += ' ';
            cmdString += args.at(i);
        }
        _pipelineCommands.push_back(std::move(cmdString));

        if (lastInChain)
        {
            // Wait for downstream processes to complete
            for (ProcessId const processPid: _currentProcessGroupPids)
            {
                int status = 0;
                waitpid(processPid, &status, 0);
                if (WIFEXITED(status))
                    _exitCode = WEXITSTATUS(status);
            }
            _currentProcessGroupPids.clear();
            _pipelineCommands.clear();

            // Reclaim terminal control
            auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
            if (!setFgResult)
                debugLog()()("Failed to reclaim foreground: {}", toString(setFgResult.error()));
        }

        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    // Handle cat builtin in pipeline
    if (program == "cat")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

        std::vector<std::string> catArgs;
        for (size_t i = 1; i < args.size(); ++i)
            catArgs.push_back(args.at(i));

        // Parse flags
        bool numberLines = false;
        bool numberNonBlank = false;
        bool squeezeBlank = false;
        bool showEnds = false;
        bool showTabs = false;
        bool showHelp = false;
        std::vector<std::string> files;

        for (size_t i = 0; i < catArgs.size(); ++i)
        {
            std::string_view arg = catArgs[i];

            if (arg == "--")
            {
                for (size_t j = i + 1; j < catArgs.size(); ++j)
                    files.push_back(catArgs[j]);
                break;
            }

            if (arg == "--help")
            {
                showHelp = true;
                continue;
            }
            if (arg == "--number")
            {
                numberLines = true;
                continue;
            }
            if (arg == "--number-nonblank")
            {
                numberNonBlank = true;
                continue;
            }
            if (arg == "--squeeze-blank")
            {
                squeezeBlank = true;
                continue;
            }
            if (arg == "--show-ends")
            {
                showEnds = true;
                continue;
            }
            if (arg == "--show-tabs")
            {
                showTabs = true;
                continue;
            }
            if (arg == "--show-all")
            {
                showEnds = true;
                showTabs = true;
                continue;
            }

            if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
            {
                bool validFlag = true;
                for (size_t j = 1; j < arg.size(); ++j)
                {
                    switch (arg[j])
                    {
                        case 'n': numberLines = true; break;
                        case 'b': numberNonBlank = true; break;
                        case 's': squeezeBlank = true; break;
                        case 'E': showEnds = true; break;
                        case 'T': showTabs = true; break;
                        case 'A':
                            showEnds = true;
                            showTabs = true;
                            break;
                        case 'h': showHelp = true; break;
                        default: validFlag = false; break;
                    }
                    if (!validFlag)
                        break;
                }
                if (validFlag)
                    continue;
            }

            files.push_back(std::string(arg));
        }

        if (numberNonBlank)
            numberLines = false;

        auto writeOutput = [stdoutFd](std::string const& str) {
            [[maybe_unused]] auto written = write(stdoutFd, str.data(), str.size());
        };

        if (showHelp)
        {
            writeOutput("Usage: cat [OPTION]... [FILE]...\n");
            writeOutput("Concatenate FILE(s) to standard output.\n");
            writeOutput("With no FILE, or when FILE is -, read standard input.\n");
            writeOutput("\n");
            writeOutput("  -n, --number           number all output lines\n");
            writeOutput("  -b, --number-nonblank  number non-blank output lines (overrides -n)\n");
            writeOutput("  -s, --squeeze-blank    suppress repeated empty output lines\n");
            writeOutput("  -E, --show-ends        display $ at end of each line\n");
            writeOutput("  -T, --show-tabs        display TAB characters as ^I\n");
            writeOutput("  -A, --show-all         equivalent to -ET\n");
            writeOutput("  -h, --help             display this help and exit\n");

            if (!lastInChain)
                _currentPipelineBuilder.closeCurrentPipeWriter();

            _exitCode = 0;
            context.setResult(CoreVM::CoreNumber(0));
            return;
        }

        int lineNumber = 1;
        bool lastLineWasBlank = false;

        auto processContent = [&](std::string const& content) {
            std::string line;
            for (size_t i = 0; i < content.size(); ++i)
            {
                char c = content[i];
                if (c == '\n')
                {
                    bool isBlank = line.empty();

                    if (squeezeBlank && isBlank && lastLineWasBlank)
                    {
                        line.clear();
                        continue;
                    }
                    lastLineWasBlank = isBlank;

                    if (showTabs)
                    {
                        std::string processed;
                        for (char ch: line)
                        {
                            if (ch == '\t')
                                processed += "^I";
                            else
                                processed += ch;
                        }
                        line = std::move(processed);
                    }

                    std::string output;
                    if (numberNonBlank && !isBlank)
                        output = std::format("{:>6}\t", lineNumber++);
                    else if (numberLines)
                        output = std::format("{:>6}\t", lineNumber++);
                    output += line;
                    if (showEnds)
                        output += '$';
                    output += '\n';
                    writeOutput(output);
                    line.clear();
                }
                else
                {
                    line += c;
                }
            }
            if (!line.empty())
            {
                if (showTabs)
                {
                    std::string processed;
                    for (char ch: line)
                    {
                        if (ch == '\t')
                            processed += "^I";
                        else
                            processed += ch;
                    }
                    line = std::move(processed);
                }

                std::string output;
                if (numberNonBlank && !line.empty())
                    output = std::format("{:>6}\t", lineNumber++);
                else if (numberLines)
                    output = std::format("{:>6}\t", lineNumber++);
                output += line;
                writeOutput(output);
            }
        };

        auto readFromFd = [](NativeHandle fd) -> std::string {
            std::string content;
            char buffer[4096];
            ssize_t bytesRead;
            while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
                content.append(buffer, static_cast<size_t>(bytesRead));
            return content;
        };

        bool success = true;

        if (files.empty())
            files.push_back("-");

        for (auto const& file: files)
        {
            if (file == "-")
            {
                std::string content = readFromFd(stdinFd);
                processContent(content);
            }
            else
            {
                auto const result = _processManager.openFile(file, O_RDONLY);
                if (!result.has_value())
                {
                    error("cat: {}: {}", file, strerror(errno));
                    success = false;
                    continue;
                }
                NativeHandle fd = result.value();
                std::string content = readFromFd(fd);
                close(fd);
                processContent(content);
            }
        }

        if (!lastInChain)
            _currentPipelineBuilder.closeCurrentPipeWriter();

        std::string cmdString = "cat";
        for (size_t i = 1; i < args.size(); ++i)
        {
            cmdString += ' ';
            cmdString += args.at(i);
        }
        _pipelineCommands.push_back(std::move(cmdString));

        if (lastInChain)
        {
            for (ProcessId const processPid: _currentProcessGroupPids)
            {
                int status = 0;
                waitpid(processPid, &status, 0);
                if (WIFEXITED(status))
                    _exitCode = WEXITSTATUS(status);
            }
            _currentProcessGroupPids.clear();
            _pipelineCommands.clear();

            auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
            if (!setFgResult)
                debugLog()()("Failed to reclaim foreground: {}", toString(setFgResult.error()));
        }

        _exitCode = success ? 0 : 1;
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    // Handle sleep builtin in pipeline
    if (program == "sleep")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

        std::vector<std::string> sleepArgs;
        for (size_t i = 1; i < args.size(); ++i)
            sleepArgs.push_back(args.at(i));

        auto writeOutput = [stdoutFd](std::string const& str) {
            [[maybe_unused]] auto written = write(stdoutFd, str.data(), str.size());
        };

        // Check for help
        for (auto const& arg: sleepArgs)
        {
            if (arg == "-h" || arg == "--help")
            {
                writeOutput("Usage: sleep NUMBER[SUFFIX]...\n");
                writeOutput("Pause for NUMBER seconds.\n");
                writeOutput("\n");
                writeOutput("SUFFIX may be:\n");
                writeOutput("  s   seconds (default)\n");
                writeOutput("  m   minutes\n");
                writeOutput("  h   hours\n");
                writeOutput("  d   days\n");
                writeOutput("\n");
                writeOutput("Multiple arguments are summed together.\n");
                writeOutput("NUMBER may be an integer or floating-point number.\n");

                if (!lastInChain)
                    _currentPipelineBuilder.closeCurrentPipeWriter();

                _exitCode = 0;
                context.setResult(CoreVM::CoreNumber(0));
                return;
            }
        }

        // No arguments - error
        if (sleepArgs.empty())
        {
            error("sleep: missing operand");

            if (!lastInChain)
                _currentPipelineBuilder.closeCurrentPipeWriter();

            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        // Parse duration arguments
        double totalSeconds = 0.0;
        bool parseError = false;
        for (auto const& arg: sleepArgs)
        {
            if (arg.empty())
                continue;

            double multiplier = 1.0;
            std::string numStr = arg;

            char lastChar = arg.back();
            if (lastChar == 's' || lastChar == 'S')
            {
                multiplier = 1.0;
                numStr = arg.substr(0, arg.size() - 1);
            }
            else if (lastChar == 'm' || lastChar == 'M')
            {
                multiplier = 60.0;
                numStr = arg.substr(0, arg.size() - 1);
            }
            else if (lastChar == 'h' || lastChar == 'H')
            {
                multiplier = 3600.0;
                numStr = arg.substr(0, arg.size() - 1);
            }
            else if (lastChar == 'd' || lastChar == 'D')
            {
                multiplier = 86400.0;
                numStr = arg.substr(0, arg.size() - 1);
            }

            if (numStr.empty())
            {
                error("sleep: invalid time interval '{}'", arg);
                parseError = true;
                break;
            }

            try
            {
                size_t pos = 0;
                double value = std::stod(numStr, &pos);
                if (pos != numStr.size() || value < 0)
                {
                    error("sleep: invalid time interval '{}'", arg);
                    parseError = true;
                    break;
                }
                totalSeconds += value * multiplier;
            }
            catch (std::exception const&)
            {
                error("sleep: invalid time interval '{}'", arg);
                parseError = true;
                break;
            }
        }

        if (parseError)
        {
            if (!lastInChain)
                _currentPipelineBuilder.closeCurrentPipeWriter();

            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        // Sleep
        if (totalSeconds > 0)
        {
            auto const duration = std::chrono::duration<double>(totalSeconds);
            std::this_thread::sleep_for(duration);
        }

        if (!lastInChain)
            _currentPipelineBuilder.closeCurrentPipeWriter();

        // Track command for job table
        std::string cmdString = "sleep";
        for (size_t i = 1; i < args.size(); ++i)
        {
            cmdString += ' ';
            cmdString += args.at(i);
        }
        _pipelineCommands.push_back(std::move(cmdString));

        if (lastInChain)
        {
            for (ProcessId const processPid: _currentProcessGroupPids)
            {
                int status = 0;
                waitpid(processPid, &status, 0);
                if (WIFEXITED(status))
                    _exitCode = WEXITSTATUS(status);
            }
            _currentProcessGroupPids.clear();
            _pipelineCommands.clear();

            auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
            if (!setFgResult)
                debugLog()()("Failed to reclaim foreground: {}", toString(setFgResult.error()));
        }

        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(args.begin() + 1, args.end());
    config.stdinFd = stdinFd;
    config.stdoutFd = stdoutFd;
    config.processGroup = !_currentProcessGroupPids.empty()
                              ? std::make_optional(_currentProcessGroupPids.front())
                              : std::make_optional<ProcessId>(0);
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    ProcessId const pid = spawnResult.value();
    _leftPid = _rightPid;
    _rightPid = pid;
    _currentProcessGroupPids.push_back(pid);

    // Track command string for job table display
    std::string cmdString;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            cmdString += ' ';
        cmdString += args.at(i);
    }
    _pipelineCommands.push_back(std::move(cmdString));

    if (lastInChain)
    {
#if !defined(_WIN32)
        // Build command string for job table from _pipelineCommands
        std::string command;
        for (size_t i = 0; i < _pipelineCommands.size(); ++i)
        {
            if (i > 0)
                command += " | ";
            command += _pipelineCommands[i];
        }

        // Process group leader is the first process
        ProcessId const pgid = _currentProcessGroupPids.front();

        // Give terminal control to the pipeline's process group
        auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), pgid);
        if (!setFgResult)
            debugLog()()("Failed to set foreground process group: {}", toString(setFgResult.error()));

        bool anyStopped = false;
        for (ProcessId const processPid: _currentProcessGroupPids)
        {
            auto const waitResult = _processManager.wait(processPid, WaitFlag::Untraced);
            if (!waitResult.has_value())
            {
                error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                continue;
            }

            _exitCode = waitResult->exitCode;
            if (waitResult->stopped)
            {
                anyStopped = true;
                debugLog()()("child process {} stopped\n", processPid);
            }
            else if (waitResult->signaled)
                debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
            else
                debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }

        // Restore shell's terminal control
        auto const restoreFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
        if (!restoreFgResult)
            debugLog()()("Failed to restore shell foreground: {}", toString(restoreFgResult.error()));

        // If any process was stopped, add the whole pipeline to job table
        if (anyStopped)
        {
            (void) jobTable.addJob(pgid, _currentProcessGroupPids, command);
            // Mark the job as stopped
            WaitResult stoppedResult { .exitCode = 0, .stopped = true };
            jobTable.updateJobState(_currentProcessGroupPids.front(), stoppedResult);
            std::println("\n[{}]+  Stopped                 {}", jobTable.getCurrentJob()->id, command);
        }

        _pipelineCommands.clear();
#else
        for (ProcessId const processPid: _currentProcessGroupPids)
        {
            auto const waitResult = _processManager.wait(processPid);
            if (!waitResult.has_value())
            {
                error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                continue;
            }

            _exitCode = waitResult->exitCode;
            if (waitResult->signaled)
                debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
            else
                debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }
#endif
        _currentProcessGroupPids.clear();
        _leftPid = std::nullopt;
        _rightPid = std::nullopt;

        cleanupProcSubst();
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinChDir(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);

    _env.set("OLDPWD", _env.get("PWD").value_or(""));
    _env.set("PWD", path);

    auto const result = _processManager.changeDirectory(path);
    if (!result.has_value())
        error("Failed to change directory to '{}': {}", path, toString(result.error()));
    else
        emitCurrentWorkingDirectory();

    context.setResult(result.has_value());
}

void Shell::builtinChDirHome(CoreVM::Params& context)
{
    auto const path = _env.get("HOME").value_or("/");
    _env.set("OLDPWD", std::filesystem::current_path().string());
    _env.set("PWD", path);

    auto const result = _processManager.changeDirectory(std::filesystem::path(path));
    if (!result.has_value())
        error("Failed to change directory to '{}': {}", path, toString(result.error()));
    else
        emitCurrentWorkingDirectory();

    context.setResult(result.has_value());
}

void Shell::builtinSet(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    context.setResult(true);
}

void Shell::builtinUnset(CoreVM::Params& context)
{
    _env.unset(context.getString(1));
    context.setResult(true);
}

void Shell::builtinGetVar(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    auto const value = _env.get(name);
    context.setResult(std::string(value.value_or("")));
}

void Shell::builtinGetExitStatus(CoreVM::Params& context)
{
    context.setResult(std::to_string(_exitCode));
}

void Shell::builtinSetExitStatus(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
}

void Shell::builtinGetProcessId(CoreVM::Params& context)
{
    context.setResult(std::to_string(_shellPid));
}

void Shell::builtinGetBackgroundId(CoreVM::Params& context)
{
    if (_lastBackgroundPid.has_value())
        context.setResult(std::to_string(_lastBackgroundPid.value()));
    else
        context.setResult("");
}

void Shell::builtinGetPositional(CoreVM::Params& context)
{
    auto const index = static_cast<size_t>(context.getInt(1));
    if (index < _positionalParameters.size())
        context.setResult(_positionalParameters[index]);
    else
        context.setResult("");
}

void Shell::builtinCmdStart(CoreVM::Params& context)
{
    _cmdBuilderStack.emplace_back();
    cmdBuilderArgs().push_back(context.getString(1));
}

void Shell::builtinCmdArg(CoreVM::Params& context)
{
    cmdBuilderArgs().push_back(context.getString(1));
}

void Shell::builtinCmdExec(CoreVM::Params& context)
{
    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);
    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    // Build command string for job table
    std::string command;
    for (size_t i = 0; i < cmdBuilderArgs().size(); ++i)
    {
        if (i > 0)
            command += ' ';
        command += cmdBuilderArgs().at(i);
    }

    auto const fgResult = runForeground(config, command);
    if (!fgResult.has_value())
    {
        error("Failed to run {}: {}", program, toString(fgResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    _exitCode = fgResult->exitCode;
    if (fgResult->stopped)
        debugLog()()("child process stopped\n");
    else
        debugLog()()("child process exited with code {}\n", _exitCode);

    cleanupProcSubst();

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinCmdExecPiped(CoreVM::Params& context)
{
    bool const lastInChain = context.getBool(1);

    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);
    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
    config.stdinFd = stdinFd;
    config.stdoutFd = stdoutFd;
    config.processGroup = !_currentProcessGroupPids.empty()
                              ? std::make_optional(_currentProcessGroupPids.front())
                              : std::make_optional<ProcessId>(0);
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    ProcessId const pid = spawnResult.value();
    _leftPid = _rightPid;
    _rightPid = pid;
    _currentProcessGroupPids.push_back(pid);

    // Track command string for job table display
    std::string cmdString;
    for (size_t i = 0; i < cmdBuilderArgs().size(); ++i)
    {
        if (i > 0)
            cmdString += ' ';
        cmdString += cmdBuilderArgs().at(i);
    }
    _pipelineCommands.push_back(std::move(cmdString));

    if (lastInChain)
    {
#if !defined(_WIN32)
        // Build command string for job table from _pipelineCommands
        std::string command;
        for (size_t i = 0; i < _pipelineCommands.size(); ++i)
        {
            if (i > 0)
                command += " | ";
            command += _pipelineCommands[i];
        }

        // Process group leader is the first process
        ProcessId const pgid = _currentProcessGroupPids.front();

        // Give terminal control to the pipeline's process group
        auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), pgid);
        if (!setFgResult)
            debugLog()()("Failed to set foreground process group: {}", toString(setFgResult.error()));

        bool anyStopped = false;
        for (ProcessId const processPid: _currentProcessGroupPids)
        {
            auto const waitResult = _processManager.wait(processPid, WaitFlag::Untraced);
            if (!waitResult.has_value())
            {
                error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                continue;
            }

            _exitCode = waitResult->exitCode;
            if (waitResult->stopped)
            {
                anyStopped = true;
                debugLog()()("child process {} stopped\n", processPid);
            }
            else if (waitResult->signaled)
                debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
            else
                debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }

        // Restore shell's terminal control
        auto const restoreFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
        if (!restoreFgResult)
            debugLog()()("Failed to restore shell foreground: {}", toString(restoreFgResult.error()));

        // If any process was stopped, add the whole pipeline to job table
        if (anyStopped)
        {
            (void) jobTable.addJob(pgid, _currentProcessGroupPids, command);
            // Mark the job as stopped
            WaitResult stoppedResult { .exitCode = 0, .stopped = true };
            jobTable.updateJobState(_currentProcessGroupPids.front(), stoppedResult);
            std::println("\n[{}]+  Stopped                 {}", jobTable.getCurrentJob()->id, command);
        }

        _pipelineCommands.clear();
#else
        for (ProcessId const processPid: _currentProcessGroupPids)
        {
            auto const waitResult = _processManager.wait(processPid);
            if (!waitResult.has_value())
            {
                error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                continue;
            }

            _exitCode = waitResult->exitCode;
            if (waitResult->signaled)
                debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
            else
                debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }
#endif
        _currentProcessGroupPids.clear();
        _leftPid = std::nullopt;
        _rightPid = std::nullopt;

        cleanupProcSubst();
    }

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinSetAndExport(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    _env.exportVariable(context.getString(1));
}

void Shell::builtinExport(CoreVM::Params& context)
{
    _env.exportVariable(context.getString(1));
}

std::vector<std::string> Shell::splitByIFS(std::string_view input) const
{
    // Get IFS, default to space/tab/newline (bash default)
    std::string const ifs = std::string(_env.get("IFS").value_or(" \t\n"));

    if (ifs.empty())
    {
        // Empty IFS = no splitting, return whole input as single element
        return { std::string(input) };
    }

    std::vector<std::string> result;
    std::string current;

    for (char c: input)
    {
        if (ifs.find(c) != std::string::npos)
        {
            // IFS character - end current word if non-empty
            if (!current.empty())
            {
                result.push_back(std::move(current));
                current.clear();
            }
            // Skip consecutive IFS characters (bash whitespace behavior)
        }
        else
        {
            current += c;
        }
    }

    // Don't forget last word
    if (!current.empty())
        result.push_back(std::move(current));

    return result;
}

std::string Shell::readInputLine(NativeHandle inputFd, ReadOptions const& options)
{
    // Display prompt if specified and we have a terminal
    bool const isTTY = (inputFd == _tty.inputFd()) && _tty.isTerminal();
    if (!options.prompt.empty() && isTTY)
    {
        _tty.writeToStdout(options.prompt);
    }

    // Set up silent mode if requested
    bool const needRestoreEcho = options.silent && isTTY;
    if (needRestoreEcho)
        _tty.setEchoEnabled(false);

    std::string result;
    bool escaped = false; // For backslash handling when !rawMode
    size_t charsRead = 0;
    bool hitEof = false;

    auto const startTime = std::chrono::steady_clock::now();

    while (true)
    {
        // Check max chars limit
        if (options.maxChars && charsRead >= *options.maxChars)
            break;

        // Calculate remaining timeout
        std::chrono::milliseconds remainingTimeout { 0 };
        if (options.timeout)
        {
            auto const elapsed = std::chrono::steady_clock::now() - startTime;
            auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
            if (elapsedMs >= *options.timeout)
            {
                // Timeout expired
                hitEof = true;
                break;
            }
            remainingTimeout = *options.timeout - elapsedMs;
        }

        // Read character
        std::optional<char> maybeChar;

        if (isTTY)
        {
            // Reading from TTY - use TTY's timeout-aware read
            maybeChar = _tty.readCharWithTimeout(remainingTimeout);
        }
        else
        {
            // Reading from pipe/file - use poll + read
            if (remainingTimeout.count() > 0)
            {
                pollfd pfd { .fd = inputFd, .events = POLLIN, .revents = 0 };
                if (poll(&pfd, 1, static_cast<int>(remainingTimeout.count())) <= 0)
                {
                    hitEof = true;
                    break; // timeout
                }
            }
            char ch;
            ssize_t const n = ::read(inputFd, &ch, 1);
            if (n <= 0)
            {
                hitEof = true;
                break; // EOF or error
            }
            maybeChar = ch;
        }

        if (!maybeChar)
        {
            hitEof = true;
            break; // EOF or timeout
        }

        char const ch = *maybeChar;
        ++charsRead;

        // Check for delimiter (unless we're in escaped state)
        if (ch == options.delimiter && !escaped)
            break;

        // Handle backslash escaping (when not in raw mode)
        if (!options.rawMode && ch == '\\' && !escaped)
        {
            escaped = true;
            continue; // Don't add backslash to result yet
        }

        if (escaped)
        {
            // In non-raw mode, backslash escapes the next character
            // Special case: backslash-newline is line continuation (skip both)
            if (ch == '\n')
            {
                escaped = false;
                // Show continuation prompt if on TTY
                if (isTTY && !options.prompt.empty())
                    _tty.writeToStdout("> ");
                continue;
            }
            // Otherwise, add the escaped character as-is (backslash removed)
            escaped = false;
        }

        result += ch;
    }

    // If we ended with an escape character, add the backslash
    if (escaped)
        result += '\\';

    // Restore echo if we disabled it
    if (needRestoreEcho)
        _tty.setEchoEnabled(true);

    // Write newline to output if silent mode was used (for visual consistency)
    if (options.silent && isTTY)
        _tty.writeToStdout("\n");

    return result;
}

void Shell::builtinReadDefault(CoreVM::Params& context)
{
    // Determine input source
    NativeHandle const inputFd =
        _redirectState.getEffectiveStdinFd(_currentPipelineBuilder.defaultStdinFd, _processManager);

    ReadOptions options;
    // Only show prompt if reading from TTY (not pipeline)
    if (inputFd == _tty.inputFd() && _tty.isTerminal())
        options.prompt = std::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m");

    std::string const line = readInputLine(inputFd, options);
    _env.set("REPLY", line);
    _exitCode = line.empty() ? 1 : 0; // Return 1 on EOF (empty input)
    context.setResult(line);
}

void Shell::builtinRead(CoreVM::Params& context)
{
    // Get arguments
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (size_t i = 0; i < argArray.size(); ++i)
            args.push_back(argArray[i]);
    }

    ReadOptions options;

    // Determine input source first (for prompt decision)
    NativeHandle const inputFd =
        _redirectState.getEffectiveStdinFd(_currentPipelineBuilder.defaultStdinFd, _processManager);
    bool const isTTY = (inputFd == _tty.inputFd()) && _tty.isTerminal();

    // Helper to write output for help
    auto writeOutput = [this](std::string const& str) {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        [[maybe_unused]] auto written = write(outputFd, str.data(), str.size());
    };

    // Parse flags
    for (size_t i = 0; i < args.size(); ++i)
    {
        std::string_view arg = args[i];

        if (arg == "-h" || arg == "--help")
        {
            writeOutput("Usage: read [OPTIONS] [VAR...]\n");
            writeOutput("Read a line from standard input.\n\n");
            writeOutput("Options:\n");
            writeOutput("  -p PROMPT   Display PROMPT before reading\n");
            writeOutput("  -r          Raw mode (don't interpret backslashes)\n");
            writeOutput("  -s          Silent mode (don't echo input)\n");
            writeOutput("  -n NCHARS   Read at most NCHARS characters\n");
            writeOutput("  -t TIMEOUT  Timeout in seconds\n");
            writeOutput("  -d DELIM    Use DELIM as line delimiter (default: newline)\n");
            writeOutput("  -h, --help  Display this help\n\n");
            writeOutput("If no VAR specified, input is stored in REPLY.\n");
            writeOutput("Multiple VARs split input by $IFS (default: space/tab/newline).\n");
            _exitCode = 0;
            context.setResult(CoreVM::CoreString(""));
            return;
        }
        else if (arg == "-p" && i + 1 < args.size())
        {
            options.prompt = args[++i];
        }
        else if (arg == "-r")
        {
            options.rawMode = true;
        }
        else if (arg == "-s")
        {
            options.silent = true;
        }
        else if (arg == "-n" && i + 1 < args.size())
        {
            try
            {
                options.maxChars = std::stoul(args[++i]);
            }
            catch (std::exception const&)
            {
                error("read: invalid number: {}", args[i]);
                _exitCode = 1;
                context.setResult(CoreVM::CoreString(""));
                return;
            }
        }
        else if (arg == "-t" && i + 1 < args.size())
        {
            try
            {
                double const seconds = std::stod(args[++i]);
                if (seconds < 0)
                {
                    error("read: invalid timeout: {}", args[i]);
                    _exitCode = 1;
                    context.setResult(CoreVM::CoreString(""));
                    return;
                }
                options.timeout = std::chrono::milliseconds(static_cast<long>(seconds * 1000));
            }
            catch (std::exception const&)
            {
                error("read: invalid timeout: {}", args[i]);
                _exitCode = 1;
                context.setResult(CoreVM::CoreString(""));
                return;
            }
        }
        else if (arg == "-d" && i + 1 < args.size())
        {
            std::string const& delim = args[++i];
            options.delimiter = delim.empty() ? '\0' : delim[0];
        }
        else if (!arg.starts_with("-"))
        {
            // Variable name
            options.variableNames.push_back(std::string(arg));
        }
        else
        {
            error("read: invalid option: {}", arg);
            _exitCode = 1;
            context.setResult(CoreVM::CoreString(""));
            return;
        }
    }

    // If no prompt specified and reading from TTY, use default prompt
    if (options.prompt.empty() && isTTY)
    {
        options.prompt = std::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m");
    }

    // Read input
    std::string const input = readInputLine(inputFd, options);

    // Determine if we hit EOF (for exit code)
    bool const hitEof = input.empty();

    // Assign to variables
    if (options.variableNames.empty())
    {
        // No variable specified - use REPLY
        _env.set("REPLY", input);
    }
    else if (options.variableNames.size() == 1)
    {
        // Single variable - no splitting needed
        _env.set(options.variableNames[0], input);
    }
    else
    {
        // Multiple variables - split by IFS
        auto const words = splitByIFS(input);

        for (size_t i = 0; i < options.variableNames.size(); ++i)
        {
            if (i < words.size())
            {
                if (i == options.variableNames.size() - 1)
                {
                    // Last variable gets remainder
                    std::string remainder;
                    for (size_t j = i; j < words.size(); ++j)
                    {
                        if (j > i)
                            remainder += ' ';
                        remainder += words[j];
                    }
                    _env.set(options.variableNames[i], remainder);
                }
                else
                {
                    _env.set(options.variableNames[i], words[i]);
                }
            }
            else
            {
                // More variables than words - set to empty
                _env.set(options.variableNames[i], "");
            }
        }
    }

    _exitCode = hitEof ? 1 : 0;
    context.setResult(CoreVM::CoreString(input));
}

void Shell::builtinOpenRead(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);
    auto const result = _processManager.openFile(path, O_RDONLY);
    if (!result.has_value())
    {
        error("Failed to open file '{}': {}", path, toString(result.error()));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    context.setResult(CoreVM::CoreNumber(result.value()));
}

void Shell::builtinOpenWrite(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);
    int const oflags = static_cast<int>(context.getInt(2));
    auto const result = _processManager.openFile(path, oflags ? oflags : (O_WRONLY | O_CREAT | O_TRUNC));
    if (!result.has_value())
    {
        error("Failed to open file '{}': {}", path, toString(result.error()));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    context.setResult(CoreVM::CoreNumber(result.value()));
}

void Shell::builtinRedirectStart(CoreVM::Params&)
{
    _redirectState.clear();
}

void Shell::builtinRedirectInput(CoreVM::Params& context)
{
    int const targetFd = static_cast<int>(context.getInt(1));
    std::string path = context.getString(2);
    _redirectState.addInputFile(targetFd, std::move(path));
}

void Shell::builtinRedirectOutput(CoreVM::Params& context)
{
    int const sourceFd = static_cast<int>(context.getInt(1));
    std::string path = context.getString(2);
    bool const append = context.getBool(3);
    _redirectState.addOutputFile(sourceFd, std::move(path), append);
}

void Shell::builtinRedirectFdDup(CoreVM::Params& context)
{
    int const sourceFd = static_cast<int>(context.getInt(1));
    int const targetFd = static_cast<int>(context.getInt(2));
    _redirectState.addFdDup(sourceFd, targetFd);
}

void Shell::builtinRedirectHeredoc(CoreVM::Params& context)
{
    int const targetFd = static_cast<int>(context.getInt(1));
    std::string content = context.getString(2);
    _redirectState.addHereDoc(targetFd, std::move(content));
}

void Shell::builtinRedirectHerestring(CoreVM::Params& context)
{
    int const targetFd = static_cast<int>(context.getInt(1));
    std::string content = context.getString(2);
    _redirectState.addHereString(targetFd, std::move(content));
}

void Shell::builtinRedirectEnd(CoreVM::Params&)
{
    for (auto& entry: _redirectState.entries)
    {
        if (entry.openedFd >= 0 && entry.openedFd != STDIN_FILENO && entry.openedFd != STDOUT_FILENO
            && entry.openedFd != STDERR_FILENO)
        {
            close(entry.openedFd);
            entry.openedFd = -1;
        }
    }
    _redirectState.clear();
}

void Shell::builtinSubstStart(CoreVM::Params&)
{
    _substitutionCapture.emplace();

    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        error("Failed to create pipe for command substitution: {}", toString(pipeResult.error()));
        _substitutionCapture.reset();
        return;
    }

    _substitutionCapture->pipe = std::move(pipeResult.value());
    _substitutionCapture->savedStdout = _currentPipelineBuilder.defaultStdoutFd;
    _currentPipelineBuilder.defaultStdoutFd = _substitutionCapture->pipe->writer();
}

void Shell::builtinSubstEnd(CoreVM::Params& context)
{
    if (!_substitutionCapture)
    {
        error("Command substitution end without matching start");
        context.setResult(std::string {});
        return;
    }

    _currentPipelineBuilder.defaultStdoutFd = _substitutionCapture->savedStdout;
    _substitutionCapture->pipe->closeWriter();

    std::string output;
    char buffer[4096];
    while (true)
    {
        ssize_t const bytesRead = read(_substitutionCapture->pipe->reader(), buffer, sizeof(buffer));
        if (bytesRead <= 0)
            break;
        output.append(buffer, static_cast<size_t>(bytesRead));
    }

    _substitutionCapture->pipe->closeReader();

    while (!output.empty() && output.back() == '\n')
        output.pop_back();

    _substitutionCapture.reset();

    context.setResult(std::move(output));
}

void Shell::builtinProcSubstFork(CoreVM::Params& context)
{
    bool const isWrite = context.getBool(1);

    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        error("Failed to create pipe for process substitution: {}", toString(pipeResult.error()));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    auto pipe = std::move(pipeResult.value());

#if !defined(_WIN32)
    pid_t const pid = fork();

    if (pid < 0)
    {
        error("Failed to fork for process substitution: {}", strerror(errno));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    if (pid == 0)
    {
        if (isWrite)
        {
            pipe->closeWriter();
            dup2(pipe->reader(), STDIN_FILENO);
            pipe->closeReader();
        }
        else
        {
            pipe->closeReader();
            dup2(pipe->writer(), STDOUT_FILENO);
            pipe->closeWriter();
        }

        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    _procSubstChildPids.push_back(static_cast<ProcessId>(pid));

    NativeHandle exposedFd;
    if (isWrite)
    {
        pipe->closeReader();
        exposedFd = pipe->releaseWriter();
    }
    else
    {
        pipe->closeWriter();
        exposedFd = pipe->releaseReader();
    }

    _procSubstExposedFds.push_back(exposedFd);

    #if defined(__linux__)
    _procSubstFdPath = std::format("/proc/self/fd/{}", exposedFd);
    #else
    _procSubstFdPath = std::format("/dev/fd/{}", exposedFd);
    #endif

    context.setResult(CoreVM::CoreNumber(1));
#else
    error("Process substitution not implemented on Windows");
    context.setResult(CoreVM::CoreNumber(-1));
#endif
}

void Shell::builtinProcSubstExit(CoreVM::Params&)
{
#if !defined(_WIN32)
    _exit(0);
#endif
}

void Shell::builtinProcSubstGetPath(CoreVM::Params& context)
{
    context.setResult(_procSubstFdPath);
}

void Shell::builtinProcSubstCleanup(CoreVM::Params&)
{
    cleanupProcSubst();
}

void Shell::builtinExpandTilde(CoreVM::Params& context)
{
    auto const& suffix = context.getString(1);
    std::string home = std::string(_env.get("HOME").value_or(""));
    context.setResult(home + suffix);
}

void Shell::builtinExpandTildeUser(CoreVM::Params& context)
{
    auto const& user = context.getString(1);
    auto const& suffix = context.getString(2);
#if !defined(_WIN32)
    if (auto* pw = getpwnam(user.c_str()); pw != nullptr)
        context.setResult(std::string(pw->pw_dir) + suffix);
    else
        context.setResult("~" + user + suffix);
#else
    context.setResult("~" + user + suffix);
#endif
}

bool Shell::globMatchFilename(std::string_view filename, std::string_view pattern)
{
    size_t fi = 0;
    size_t pi = 0;
    size_t starIdx = std::string_view::npos;
    size_t matchIdx = 0;

    while (fi < filename.size())
    {
        if (pi < pattern.size() && pattern[pi] == '*')
        {
            starIdx = pi;
            matchIdx = fi;
            ++pi;
        }
        else if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == filename[fi]))
        {
            ++fi;
            ++pi;
        }
        else if (pi < pattern.size() && pattern[pi] == '[')
        {
            bool negate = false;
            bool matched = false;
            ++pi;
            if (pi < pattern.size() && (pattern[pi] == '!' || pattern[pi] == '^'))
            {
                negate = true;
                ++pi;
            }
            auto const bracketStart = pi;
            while (pi < pattern.size() && pattern[pi] != ']')
            {
                if (pi + 2 < pattern.size() && pattern[pi + 1] == '-' && pattern[pi + 2] != ']')
                {
                    if (filename[fi] >= pattern[pi] && filename[fi] <= pattern[pi + 2])
                        matched = true;
                    pi += 3;
                }
                else
                {
                    if (filename[fi] == pattern[pi])
                        matched = true;
                    ++pi;
                }
            }
            if (pi < pattern.size())
                ++pi;

            if (negate)
                matched = !matched;
            if (!matched)
            {
                if (starIdx != std::string_view::npos)
                {
                    pi = starIdx + 1;
                    ++matchIdx;
                    fi = matchIdx;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                ++fi;
            }
        }
        else if (starIdx != std::string_view::npos)
        {
            pi = starIdx + 1;
            ++matchIdx;
            fi = matchIdx;
        }
        else
        {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
        ++pi;

    return pi == pattern.size();
}

std::vector<std::string> Shell::expandGlobPattern(std::string_view pattern)
{
    namespace fs = std::filesystem;
    std::vector<std::string> results;
    std::string patternStr(pattern);

    auto const starstarPos = patternStr.find("**");
    if (starstarPos != std::string::npos)
    {
        return expandRecursiveGlob(patternStr);
    }

    fs::path patternPath(patternStr);

    fs::path dirPath = patternPath.parent_path();
    std::string filePattern = patternPath.filename().string();

    if (dirPath.empty())
        dirPath = ".";

    bool hasGlobChars = filePattern.find_first_of("*?[") != std::string::npos;

    if (!hasGlobChars)
    {
        return {};
    }

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || ec)
    {
        return {};
    }

    for (auto const& entry: fs::directory_iterator(dirPath, ec))
    {
        if (ec)
            break;

        std::string filename = entry.path().filename().string();
        if (globMatchFilename(filename, filePattern))
        {
            if (dirPath == ".")
                results.push_back(filename);
            else
                results.push_back(entry.path().string());
        }
    }

    std::ranges::sort(results);

    return results;
}

std::vector<std::string> Shell::expandRecursiveGlob(std::string_view pattern)
{
    namespace fs = std::filesystem;
    std::vector<std::string> results;
    std::string patternStr(pattern);

    auto const starstarPos = patternStr.find("**");
    if (starstarPos == std::string::npos)
        return {};

    std::string basePath = patternStr.substr(0, starstarPos);
    while (!basePath.empty() && (basePath.back() == '/' || basePath.back() == '\\'))
        basePath.pop_back();
    if (basePath.empty())
        basePath = ".";

    std::string suffixPattern = patternStr.substr(starstarPos + 2);
    while (!suffixPattern.empty() && (suffixPattern.front() == '/' || suffixPattern.front() == '\\'))
        suffixPattern.erase(0, 1);

    std::error_code ec;
    if (!fs::exists(basePath, ec) || ec)
        return {};

    for (auto const& entry: fs::recursive_directory_iterator(basePath, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        std::string filePath = entry.path().string();
        std::string filename = entry.path().filename().string();

        if (!suffixPattern.empty())
        {
            if (globMatchFilename(filename, suffixPattern))
                results.push_back(filePath);
        }
        else
        {
            results.push_back(filePath);
        }
    }

    std::ranges::sort(results);

    return results;
}

void Shell::builtinExpandGlob(CoreVM::Params& context)
{
    auto const& pattern = context.getString(1);

    auto matches = expandGlobPattern(pattern);
    if (matches.empty())
    {
        cmdBuilderArgs().push_back(pattern);
    }
    else
    {
        for (auto& match: matches)
            cmdBuilderArgs().push_back(std::move(match));
    }
}

void Shell::builtinArithToString(CoreVM::Params& context)
{
    auto const unsignedValue = context.getInt(1);
    auto const signedValue = static_cast<int64_t>(unsignedValue);
    context.setResult(std::to_string(signedValue));
}

void Shell::builtinArithGetVar(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    auto const value = _env.get(name);
    if (!value.has_value() || value->empty())
    {
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }
    int64_t result = 0;
    auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), result);
    context.setResult(CoreVM::CoreNumber(result));
}

void Shell::builtinArithPow(CoreVM::Params& context)
{
    auto const base = context.getInt(1);
    auto const exp = context.getInt(2);
    if (exp < 0)
    {
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }
    int64_t result = 1;
    for (int64_t i = 0; i < exp; ++i)
        result *= base;
    context.setResult(CoreVM::CoreNumber(result));
}

void Shell::builtinExpandParamLength(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const value = _env.get(varName);
    context.setResult(std::to_string(value.value_or("").size()));
}

void Shell::builtinExpandParamDefault(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& defaultValue = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(std::string(*value));
    else
        context.setResult(defaultValue);
}

void Shell::builtinExpandParamAlternate(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& alternate = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(alternate);
    else
        context.setResult("");
}

void Shell::builtinExpandParamAssign(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& defaultValue = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(std::string(*value));
    else
    {
        _env.set(varName, defaultValue);
        context.setResult(defaultValue);
    }
}

void Shell::builtinExpandParamError(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& errorMsg = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(std::string(*value));
    else
    {
        std::string const msg = errorMsg.empty() ? std::format("{}: parameter null or not set", varName)
                                                 : std::format("{}: {}", varName, errorMsg);
        error("{}", msg);
        _exitCode = 1;
        context.setResult("");
    }
}

bool Shell::globMatch(std::string_view text, std::string_view pattern)
{
    size_t ti = 0;
    size_t pi = 0;
    size_t starIdx = std::string_view::npos;
    size_t matchIdx = 0;

    while (ti < text.size())
    {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti]))
        {
            ++ti;
            ++pi;
        }
        else if (pi < pattern.size() && pattern[pi] == '*')
        {
            starIdx = pi;
            matchIdx = ti;
            ++pi;
        }
        else if (starIdx != std::string_view::npos)
        {
            pi = starIdx + 1;
            ++matchIdx;
            ti = matchIdx;
        }
        else
        {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
        ++pi;

    return pi == pattern.size();
}

std::vector<size_t> Shell::findPrefixMatches(std::string_view text, std::string_view pattern)
{
    std::vector<size_t> matches;

    for (size_t len = 0; len <= text.size(); ++len)
    {
        if (globMatch(text.substr(0, len), pattern))
            matches.push_back(len);
    }

    return matches;
}

std::vector<size_t> Shell::findSuffixMatches(std::string_view text, std::string_view pattern)
{
    std::vector<size_t> matches;

    for (size_t start = 0; start <= text.size(); ++start)
    {
        if (globMatch(text.substr(start), pattern))
            matches.push_back(start);
    }

    return matches;
}

std::optional<size_t> Shell::findPatternMatchLength(std::string_view text, std::string_view pattern)
{
    for (size_t len = 1; len <= text.size(); ++len)
    {
        if (globMatch(text.substr(0, len), pattern))
            return len;
    }
    return std::nullopt;
}

void Shell::builtinExpandParamRemovePrefix(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& pattern = context.getString(2);
    bool const longest = context.getBool(3);
    auto const value = _env.get(varName);
    std::string const val = std::string(value.value_or(""));

    if (val.empty() || pattern.empty())
    {
        context.setResult(val);
        return;
    }

    auto const matches = findPrefixMatches(val, pattern);
    if (matches.empty())
    {
        context.setResult(val);
        return;
    }

    size_t const matchLen = longest ? matches.back() : matches.front();
    context.setResult(val.substr(matchLen));
}

void Shell::builtinExpandParamRemoveSuffix(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& pattern = context.getString(2);
    bool const longest = context.getBool(3);
    auto const value = _env.get(varName);
    std::string const val = std::string(value.value_or(""));

    if (val.empty() || pattern.empty())
    {
        context.setResult(val);
        return;
    }

    auto const matches = findSuffixMatches(val, pattern);
    if (matches.empty())
    {
        context.setResult(val);
        return;
    }

    size_t const matchStart = longest ? matches.front() : matches.back();
    context.setResult(val.substr(0, matchStart));
}

void Shell::builtinExpandParamReplace(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& pattern = context.getString(2);
    auto const& replacement = context.getString(3);
    bool const replaceAll = context.getBool(4);
    auto const value = _env.get(varName);
    std::string const val = std::string(value.value_or(""));

    if (val.empty() || pattern.empty())
    {
        context.setResult(val);
        return;
    }

    std::string result;
    size_t pos = 0;

    while (pos < val.size())
    {
        auto const matchResult = findPatternMatchLength(std::string_view(val).substr(pos), pattern);
        if (matchResult.has_value())
        {
            result += replacement;
            pos += *matchResult;
            if (!replaceAll)
            {
                result += val.substr(pos);
                break;
            }
        }
        else
        {
            result += val[pos];
            ++pos;
        }
    }

    context.setResult(result);
}

void Shell::builtinForInit(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    _forLoopStack.emplace_back();
    _forLoopStack.back().variable = varName;
    _forLoopStack.back().index = 0;
}

void Shell::builtinForAddItem(CoreVM::Params& context)
{
    if (_forLoopStack.empty())
        return;
    auto const& item = context.getString(1);
    _forLoopStack.back().items.push_back(item);
}

void Shell::builtinForHasMore(CoreVM::Params& context)
{
    if (_forLoopStack.empty())
    {
        context.setResult(false);
        return;
    }
    auto const& state = _forLoopStack.back();
    context.setResult(state.index < state.items.size());
}

void Shell::builtinForNext(CoreVM::Params& context)
{
    if (_forLoopStack.empty())
        return;

    auto& state = _forLoopStack.back();
    auto const& varName = context.getString(1);

    if (state.index < state.items.size())
    {
        _env.set(varName, state.items[state.index]);
        ++state.index;
    }
}

void Shell::builtinForCleanup([[maybe_unused]] CoreVM::Params& context)
{
    if (!_forLoopStack.empty())
        _forLoopStack.pop_back();
}

void Shell::builtinCaseMatch(CoreVM::Params& context)
{
    auto const& word = context.getString(1);
    auto const& pattern = context.getString(2);

    bool const matched = globMatchFilename(word, pattern);
    context.setResult(matched);
}

void Shell::builtinFunctionRegister(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    _registeredFunctions.insert(name);
}

void Shell::builtinFunctionCall(CoreVM::Params& context)
{
    auto const& name = context.getString(1);

    if (!_registeredFunctions.contains(name))
    {
        error("{}: command not found", name);
        _exitCode = 127;
        context.setResult(CoreVM::CoreNumber(127));
        return;
    }

    CoreVM::Handler* handler = _currentProgram->findHandler(name);
    if (!handler)
    {
        error("{}: function not found (was it defined in a previous command?)", name);
        _exitCode = 127;
        context.setResult(CoreVM::CoreNumber(127));
        return;
    }

    auto runner = CoreVM::Runner(handler,
                                 nullptr,
                                 &_globals,
                                 CoreVM::RuntimeConfig::defaultConfig(),
                                 std::bind(&Shell::trace, this, _1, _2, _3));
    runner.run();
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinJobs(CoreVM::Params& context)
{
    auto const jobs = jobTable.listJobs();
    for (auto const* job: jobs)
    {
        char const marker = (jobTable.getCurrentJob() && job->id == jobTable.getCurrentJob()->id)     ? '+'
                            : (jobTable.getPreviousJob() && job->id == jobTable.getPreviousJob()->id) ? '-'
                                                                                                      : ' ';
        std::string stateStr;
        switch (job->state)
        {
            case JobState::Running: stateStr = "Running"; break;
            case JobState::Stopped: stateStr = "Stopped"; break;
            case JobState::Done: stateStr = "Done"; break;
            case JobState::Terminated: stateStr = std::format("Terminated ({})", job->signal); break;
        }

        std::println("[{}]{} {}\t{}", job->id, marker, stateStr, job->command);
    }

    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
}

void Shell::builtinFg(CoreVM::Params& context)
{
#if !defined(_WIN32)
    // Get job to foreground
    Job* job = nullptr;
    if (context.count() > 1)
    {
        int const jobId = static_cast<int>(context.getInt(1));
        job = jobTable.getJob(jobId);
        if (!job)
        {
            error("fg: %{}: no such job", jobId);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }
    else
    {
        job = jobTable.getCurrentJob();
        if (!job)
        {
            error("fg: no current job");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    // Print the command being resumed
    std::println("{}", job->command);

    // Give the job's process group control of the terminal
    auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), job->pgid);
    if (!setFgResult.has_value())
    {
        error("fg: failed to set foreground process group: {}", toString(setFgResult.error()));
    }

    // If the job was stopped, send SIGCONT
    if (job->state == JobState::Stopped)
    {
        auto const sigResult = _processManager.sendSignal(-static_cast<int>(job->pgid), SIGCONT);
        if (!sigResult.has_value())
        {
            error("fg: failed to send SIGCONT: {}", toString(sigResult.error()));
        }
        job->state = JobState::Running;
    }

    // Wait for the job to complete or stop
    for (ProcessId const pid: job->pids)
    {
        int status = 0;
        pid_t const waitedPid = waitpid(static_cast<pid_t>(pid), &status, WUNTRACED);
        if (waitedPid > 0)
        {
            WaitResult result;
            if (WIFEXITED(status))
            {
                result.exitCode = WEXITSTATUS(status);
                _exitCode = result.exitCode;
            }
            else if (WIFSIGNALED(status))
            {
                result.signaled = true;
                result.signal = WTERMSIG(status);
                _exitCode = 128 + result.signal;
            }
            else if (WIFSTOPPED(status))
            {
                result.stopped = true;
                result.signal = WSTOPSIG(status);
            }
            jobTable.updateJobState(pid, result);
        }
    }

    // Restore shell's terminal control
    auto const restoreResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
    if (!restoreResult.has_value())
    {
        error("fg: failed to restore shell foreground: {}", toString(restoreResult.error()));
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
#else
    error("fg: not supported on Windows");
    _exitCode = 1;
    context.setResult(CoreVM::CoreNumber(1));
#endif
}

void Shell::builtinBg(CoreVM::Params& context)
{
#if !defined(_WIN32)
    // Get job to background
    Job* job = nullptr;
    if (context.count() > 1)
    {
        int const jobId = static_cast<int>(context.getInt(1));
        job = jobTable.getJob(jobId);
        if (!job)
        {
            error("bg: %{}: no such job", jobId);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }
    else
    {
        job = jobTable.getCurrentJob();
        if (!job)
        {
            error("bg: no current job");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    if (job->state != JobState::Stopped)
    {
        error("bg: job {} not stopped", job->id);
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    // Print the command being resumed
    std::println("[{}]+ {} &", job->id, job->command);

    // Send SIGCONT to the process group
    auto const sigResult = _processManager.sendSignal(-static_cast<int>(job->pgid), SIGCONT);
    if (!sigResult.has_value())
    {
        error("bg: failed to send SIGCONT: {}", toString(sigResult.error()));
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    job->state = JobState::Running;
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
#else
    error("bg: not supported on Windows");
    _exitCode = 1;
    context.setResult(CoreVM::CoreNumber(1));
#endif
}

void Shell::builtinWait(CoreVM::Params& context)
{
#if !defined(_WIN32)
    if (context.count() >= 1)
    {
        // Wait for specific job
        int const jobId = static_cast<int>(context.getInt(1));
        Job* job = jobTable.getJob(jobId);
        if (!job)
        {
            error("wait: %{}: no such job", jobId);
            _exitCode = 127;
            context.setResult(CoreVM::CoreNumber(127));
            return;
        }

        // Wait for all processes in the job
        for (ProcessId const pid: job->pids)
        {
            int status = 0;
            waitpid(static_cast<pid_t>(pid), &status, 0);
            if (WIFEXITED(status))
                _exitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                _exitCode = 128 + WTERMSIG(status);
        }

        job->state = JobState::Done;
        job->exitCode = _exitCode;
    }
    else
    {
        // Wait for all background jobs
        auto jobs = jobTable.listJobs();
        for (auto const* constJob: jobs)
        {
            if (constJob->state != JobState::Running && constJob->state != JobState::Stopped)
                continue;

            // Get mutable job
            Job* job = jobTable.getJob(constJob->id);
            if (!job)
                continue;

            for (ProcessId const pid: job->pids)
            {
                int status = 0;
                waitpid(static_cast<pid_t>(pid), &status, 0);
                if (WIFEXITED(status))
                    _exitCode = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    _exitCode = 128 + WTERMSIG(status);
            }

            job->state = JobState::Done;
            job->exitCode = _exitCode;
        }
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
#else
    error("wait: not supported on Windows");
    _exitCode = 1;
    context.setResult(CoreVM::CoreNumber(1));
#endif
}

void Shell::builtinCmdExecPipedBackground(CoreVM::Params& context)
{
#if !defined(_WIN32)
    std::string const command = context.getString(1);

    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);
    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    // Close any existing pipeline pipe (background jobs start fresh)
    _currentPipelineBuilder.currentPipe.reset();

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.processGroup = std::make_optional<ProcessId>(0); // Create new process group
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    ProcessId const pid = spawnResult.value();
    _lastBackgroundPid = pid;

    // Add to job table
    std::vector<ProcessId> pids;
    pids.push_back(pid);
    int const jobId = jobTable.addJob(pid, std::move(pids), command);

    // Print job info
    std::println("[{}] {}", jobId, pid);

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();

    // Background jobs return 0 immediately
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
#else
    error("Background execution not supported on Windows");
    context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
#endif
}

void Shell::builtinBind(CoreVM::Params& context)
{
    // Get arguments (may be empty if called without arguments)
    // context.count() is the number of parameters (excluding return value at index 0)
    // For bind(s)I, we have 1 parameter (the string array) at index 1
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (size_t i = 0; i < argArray.size(); ++i)
            args.push_back(argArray[i]);
    }

    // No arguments: list all bindings
    if (args.empty())
    {
        auto const& bindings = prompt.keyBindings().bindings();
        for (auto const& [chord, action]: bindings)
        {
            std::println("{}\t{}", chord.toString(), tui::editActionToString(action));
        }
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Check for flags
    if (args[0] == "-r" || args[0] == "--remove")
    {
        // Remove binding: bind -r <key>
        if (args.size() < 2)
        {
            error("bind: -r requires a key argument");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        auto const chord = tui::KeyChord::parse(args[1]);
        if (!chord)
        {
            error("bind: invalid key chord: {}", args[1]);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        prompt.unbindKey(*chord);
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    if (args[0] == "--reset")
    {
        // Reset to defaults: bind --reset
        prompt.resetKeyBindings();
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    if (args[0] == "-l" || args[0] == "--list")
    {
        // List bindings (same as no arguments)
        auto const& bindings = prompt.keyBindings().bindings();
        for (auto const& [chord, action]: bindings)
        {
            std::println("{}\t{}", chord.toString(), tui::editActionToString(action));
        }
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    if (args[0] == "-h" || args[0] == "--help")
    {
        std::println("Usage: bind [options] [key action]");
        std::println("");
        std::println("Options:");
        std::println("  -l, --list    List all keybindings");
        std::println("  -r, --remove  Remove a keybinding: bind -r ctrl+y");
        std::println("  --reset       Reset all keybindings to defaults");
        std::println("  -h, --help    Show this help message");
        std::println("");
        std::println("Examples:");
        std::println("  bind                     # List all bindings");
        std::println("  bind ctrl+y redo         # Bind Ctrl+Y to redo");
        std::println("  bind ctrl+y yank         # Bind Ctrl+Y to yank (Emacs-style)");
        std::println("  bind -r ctrl+y           # Remove Ctrl+Y binding");
        std::println("  bind --reset             # Reset to defaults");
        std::println("");
        std::println("Key format: [modifier+]...key");
        std::println("  Modifiers: ctrl, alt, shift, super");
        std::println("  Keys: a-z, enter, backspace, delete, tab, escape,");
        std::println("        up, down, left, right, home, end, f1-f12");
        std::println("");
        std::println("Actions:");
        std::println("  Movement: move-forward-char, move-backward-char, move-forward-word,");
        std::println("            move-backward-word, move-to-line-start, move-to-line-end,");
        std::println("            move-to-buffer-start, move-to-buffer-end, move-up, move-down");
        std::println("  Editing:  delete-char-backward, delete-char-forward, delete-word,");
        std::println("            delete-word-backward, kill-to-end, kill-to-start, transpose");
        std::println("  Undo:     undo, redo");
        std::println("  Kill Ring: yank, yank-pop");
        std::println("  Selection: select-all");
        std::println("  Clipboard: cut, copy, paste");
        std::println("  Control:  submit, abort, insert-newline");
        std::println("  History:  history-prev, history-next");
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Set binding: bind <key> <action>
    if (args.size() < 2)
    {
        error("bind: requires key and action arguments");
        error("Usage: bind <key> <action>");
        error("Run 'bind --help' for more information.");
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    auto const chord = tui::KeyChord::parse(args[0]);
    if (!chord)
    {
        error("bind: invalid key chord: {}", args[0]);
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    auto const action = tui::parseEditAction(args[1]);
    if (!action)
    {
        error("bind: unknown action: {}", args[1]);
        error("Run 'bind --help' to see available actions.");
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    prompt.bindKey(*chord, *action);
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
}

void Shell::builtinWhich(CoreVM::Params& context)
{
    // Get arguments (may be empty if called without arguments)
    // context.count() is the number of parameters (excluding return value at index 0)
    // For which(s)I, we have 1 parameter (the string array) at index 1
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (size_t i = 0; i < argArray.size(); ++i)
            args.push_back(argArray[i]);
    }

    // Parse flags
    bool showAll = false;
    bool showHelp = false;
    bool readAlias = false;
    std::vector<std::string> programs;

    for (auto const& arg: args)
    {
        if (arg == "-h" || arg == "--help")
            showHelp = true;
        else if (arg == "-a" || arg == "--all")
            showAll = true;
        else if (arg == "-i" || arg == "--read-alias")
            readAlias = true;
        else if (arg.starts_with("-"))
        {
            error("which: invalid option: {}", arg);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
        else
            programs.push_back(arg);
    }

    // Helper to write output to the effective stdout (respects redirects and test environments)
    auto writeOutput = [this](std::string const& str) {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        [[maybe_unused]] auto written = write(outputFd, str.data(), str.size());
    };

    // Show help if requested or no arguments given
    if (showHelp || programs.empty())
    {
        std::string help = "Usage: which [OPTIONS] PROGRAM...\n"
                           "\n"
                           "Locate executables in the PATH.\n"
                           "\n"
                           "Options:\n"
                           "  -a, --all         Print all matching executables in PATH, not just the first\n"
                           "  -h, --help        Show this help message\n"
                           "  -i, --read-alias  Also show aliases (not yet implemented)\n"
                           "\n"
                           "Exit status:\n"
                           "  0  if all programs were found\n"
                           "  1  if one or more programs were not found\n";
        writeOutput(help);
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Warn about --read-alias since aliases aren't implemented yet
    if (readAlias)
    {
        error("which: --read-alias: aliases not yet implemented");
    }

    // Get PATH
    auto const pathEnv = _env.get("PATH");
    if (!pathEnv.has_value())
    {
        error("which: PATH not set");
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    auto const paths = crispy::split(pathEnv.value(), ':');
    bool allFound = true;

    // Search for each program
    for (auto const& program: programs)
    {
        bool found = false;

        // If program contains '/', treat as path
        if (program.contains('/'))
        {
            if (std::filesystem::exists(program))
            {
                writeOutput(program + "\n");
                found = true;
            }
        }
        else
        {
            // Search PATH
            for (auto const& pathStr: paths)
            {
                auto const programPath = std::filesystem::path(pathStr) / program;
                if (std::filesystem::exists(programPath))
                {
                    writeOutput(programPath.string() + "\n");
                    found = true;
                    if (!showAll)
                        break; // Only show first match unless -a is specified
                }
            }
        }

        if (!found)
        {
            error("which: no {} in ({})", program, pathEnv.value());
            allFound = false;
        }
    }

    _exitCode = allFound ? 0 : 1;
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

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
#endif
}

void Shell::reportJobStatus()
{
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

void Shell::cleanupProcSubst()
{
#if !defined(_WIN32)
    for (ProcessId childPid: _procSubstChildPids)
    {
        int status = 0;
        waitpid(static_cast<pid_t>(childPid), &status, 0);
    }
    _procSubstChildPids.clear();

    for (NativeHandle fd: _procSubstExposedFds)
    {
        if (fd >= 0)
            close(fd);
    }
    _procSubstExposedFds.clear();

    _procSubstFdPath.clear();
#endif
}

std::expected<Shell::ForegroundResult, ShellError> Shell::runForeground(SpawnConfig& config,
                                                                        std::string const& command)
{
#if !defined(_WIN32)
    // Create new process group with child as leader
    config.processGroup = 0;

    auto spawnResult = _processManager.spawn(config);
    if (!spawnResult)
        return std::unexpected(spawnResult.error());

    ProcessId const pid = spawnResult.value();
    ProcessId const pgid = pid; // Child is process group leader

    // Give terminal control to child's process group
    auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), pgid);
    if (!setFgResult)
    {
        debugLog()()("Failed to set foreground process group: {}", toString(setFgResult.error()));
    }

    // Wait with WUNTRACED to detect stopped processes (Ctrl+Z)
    auto waitResult = _processManager.wait(pid, WaitFlag::Untraced);

    // Restore shell's terminal control
    auto const restoreFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
    if (!restoreFgResult)
    {
        debugLog()()("Failed to restore shell foreground: {}", toString(restoreFgResult.error()));
    }

    if (!waitResult)
        return std::unexpected(waitResult.error());

    ForegroundResult result {
        .exitCode = waitResult->exitCode,
        .stopped = waitResult->stopped,
        .pid = pid,
        .pgid = pgid,
    };

    // If process was stopped, add to job table
    if (waitResult->stopped)
    {
        (void) jobTable.addJob(pgid, { pid }, command);
        jobTable.updateJobState(pid, *waitResult);
        std::println("\n[{}]+  Stopped                 {}", jobTable.getCurrentJob()->id, command);
    }

    return result;
#else
    // Windows: no job control, just spawn and wait
    auto spawnResult = _processManager.spawn(config);
    if (!spawnResult)
        return std::unexpected(spawnResult.error());

    auto waitResult = _processManager.wait(spawnResult.value());
    if (!waitResult)
        return std::unexpected(waitResult.error());

    return ForegroundResult {
        .exitCode = waitResult->exitCode,
        .stopped = false,
        .pid = spawnResult.value(),
        .pgid = 0,
    };
#endif
}

void Shell::applyRedirects(SpawnConfig& config)
{
    for (auto& entry: _redirectState.entries)
    {
        switch (entry.type)
        {
            case RedirectState::Type::InputFile: {
                auto const result = _processManager.openFile(entry.path, O_RDONLY);
                if (!result.has_value())
                {
                    error("Failed to open '{}' for reading: {}", entry.path, toString(result.error()));
                    continue;
                }
                entry.openedFd = result.value();
                if (entry.targetFd == STDIN_FILENO)
                    config.stdinFd = entry.openedFd;
                break;
            }
            case RedirectState::Type::OutputFile: {
                int const oflags =
                    entry.append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                auto const result = _processManager.openFile(entry.path, oflags);
                if (!result.has_value())
                {
                    error("Failed to open '{}' for writing: {}", entry.path, toString(result.error()));
                    continue;
                }
                entry.openedFd = result.value();
                if (entry.sourceFd == STDOUT_FILENO)
                    config.stdoutFd = entry.openedFd;
                else if (entry.sourceFd == STDERR_FILENO)
                    config.stderrFd = entry.openedFd;
                break;
            }
            case RedirectState::Type::FdDup: {
                if (entry.sourceFd == STDERR_FILENO && entry.targetFd == STDOUT_FILENO)
                    config.stderrFd = config.stdoutFd;
                else if (entry.sourceFd == STDOUT_FILENO && entry.targetFd == STDERR_FILENO)
                    config.stdoutFd = config.stderrFd;
                break;
            }
            case RedirectState::Type::HereDoc:
            case RedirectState::Type::HereString: {
                auto pipeResult = createPipe();
                if (!pipeResult.has_value())
                {
                    error("Failed to create pipe for here-string: {}", toString(pipeResult.error()));
                    continue;
                }
                auto pipe = std::move(pipeResult.value());

                std::string const& content = entry.content;
                ssize_t const written = write(pipe->writer(), content.data(), content.size());
                if (written < 0)
                {
                    error("Failed to write to here-string pipe: {}", strerror(errno));
                    continue;
                }

                if (entry.type == RedirectState::Type::HereString && !content.empty()
                    && content.back() != '\n')
                {
                    write(pipe->writer(), "\n", 1);
                }

                pipe->closeWriter();

                entry.openedFd = pipe->releaseReader();
                if (entry.targetFd == STDIN_FILENO)
                    config.stdinFd = entry.openedFd;
                break;
            }
        }
    }
}

std::expected<std::filesystem::path, ShellError> Shell::resolveProgram(std::string const& program) const
{
    if (program.contains('/'))
    {
        if (std::filesystem::exists(program))
            return std::filesystem::path(program);
        return std::unexpected(ShellError::ProgramNotFound);
    }

    auto const pathEnv = _env.get("PATH");
    if (!pathEnv.has_value())
        return std::unexpected(ShellError::VariableNotFound);

    auto const pathEnvValue = pathEnv.value();
    auto const paths = crispy::split(pathEnvValue, ':');

    for (auto const& pathStr: paths)
    {
        auto const programPath = std::filesystem::path(pathStr) / program;
        if (std::filesystem::exists(programPath))
        {
            debugLog()()("Found program: {}", programPath.string());
            return programPath;
        }
    }

    return std::unexpected(ShellError::ProgramNotFound);
}

void Shell::trace(CoreVM::Instruction instr, size_t ip, size_t sp)
{
    traceLog()()("{}\n", CoreVM::disassemble(instr, ip, sp, &_currentProgram->constants()));
}

std::vector<std::string>& Shell::cmdBuilderArgs()
{
    if (_cmdBuilderStack.empty())
        _cmdBuilderStack.emplace_back();
    return _cmdBuilderStack.back();
}

void Shell::builtinPrint(CoreVM::Params& context)
{
    std::string const& text = context.getString(1);
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
    [[maybe_unused]] auto written = write(outputFd, text.data(), text.size());
}

void Shell::builtinPrintln(CoreVM::Params& context)
{
    std::string const& text = context.getString(1);
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
    [[maybe_unused]] auto written = write(outputFd, text.data(), text.size());
    written = write(outputFd, "\n", 1);
}

void Shell::builtinDisplayResult(CoreVM::Params& context)
{
    auto rawVal = static_cast<uint64_t>(context.getInt(1));
    auto* runner = context.caller();
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);

    bool const useColor = isatty(outputFd) != 0;

    // Check if this is a list of records — if so, render as table
    if (runner->isKnownObject(rawVal))
    {
        auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawVal));
        if (obj->type->id == CoreVM::BuiltinTypeId::List && isListOfRecords(obj, runner))
        {
            TableConfig config;
            config.style = useColor ? TableStyle::Bordered : TableStyle::Plain;
            config.useColor = useColor;
            if (useColor)
            {
                if (auto size = _tty.getSize(); size.has_value())
                    config.terminalWidth = size->cols;
            }
            auto table = formatRecordTable(obj, runner, config);
            [[maybe_unused]] auto written = write(outputFd, table.data(), table.size());
            return;
        }
    }

    // Fallback: convert to string and print with newline
    auto str = valueToString(rawVal, runner);
    str += '\n';
    [[maybe_unused]] auto written = write(outputFd, str.data(), str.size());
}

} // namespace endo
