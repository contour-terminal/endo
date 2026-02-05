// SPDX-License-Identifier: Apache-2.0
#include "Shell.hpp"
#include <shell/ProcessGroup.hpp>

#include <CoreVM/CoreVM.hpp>

#include <crispy/assert.h>
#include <crispy/utils.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <set>

#include "ASTPrinter.hpp"
#include "Error.hpp"
#include "IRGenerator.hpp"
#include "Lexer.hpp"
#include "LogConfig.hpp"
#include "Parser.hpp"
#include "Pipe.hpp"
#include "Platform.hpp"
#include "Process.hpp"
#include "Prompt.hpp"
#include "TTY.hpp"

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <poll.h>
    #include <pwd.h>
    #include <unistd.h>
#endif

namespace
{
// Use function-local static to avoid C++20 module static initialization issues
auto& debugLog()
{
    static auto instance =
        logstore::category("shell.debug", "Shell debug log", endo::log::categoryState("shell.debug"));
    return instance;
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

int Shell::run()
{
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

        // Display the prompt before waiting for input
        prompt.display();

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

            _exitCode = execute(lineBuffer);
        }
    }
#else
    // Windows fallback: simple loop without poll
    while (!_quit && prompt.ready())
    {
        auto const lineBuffer = prompt.read();
        debugLog()()("input buffer: {}", lineBuffer);

        _exitCode = execute(lineBuffer);
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
        auto parser = endo::Parser(*this, report, std::make_unique<endo::StringSource>(lineBuffer));
        auto const rootNode = parser.parse();

        // Check for parser errors
        if (report.containsFailures())
            return EXIT_FAILURE;

        if (!rootNode)
            return EXIT_FAILURE;

        debugLog()()("Parsed & printed: {}", endo::ast::ASTPrinter::print(*rootNode));

        auto irProgram = IRGenerator::generate(*rootNode, report, *this);

        // Check for IR generation errors
        if (report.containsFailures())
            return EXIT_FAILURE;

        if (!irProgram)
            return EXIT_FAILURE;

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

        debugLog()()("================================================\n");
        debugLog()()("Optimized IR program:\n");
        if (debugLog().is_enabled())
            irProgram->dump();

        _currentProgram = CoreVM::TargetCodeGenerator {}.generate(irProgram.get());
        if (!_currentProgram)
        {
            error("Failed to generate target code");
            return EXIT_FAILURE;
        }
        _currentProgram->link(this, &report);

        debugLog()()("================================================\n");
        debugLog()()("Linked target code:\n");
        if (debugLog().is_enabled())
            _currentProgram->dump();

        CoreVM::Handler* main = _currentProgram->findHandler("@main");
        assert(main != nullptr);
        auto runner = CoreVM::Runner(main, nullptr, &_globals, std::bind(&Shell::trace, this, _1, _2, _3));
        _runner = &runner;
        runner.run();
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
    registerFunction("exit")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExit, this);

    registerFunction("export")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExport, this);

    registerFunction("export")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSetAndExport, this);

    registerFunction("true")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinTrue, this);

    registerFunction("false")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinFalse, this);

    registerFunction("cd")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinChDirHome, this);

    registerFunction("cd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinChDir, this);

    registerFunction("set")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinSet, this);

    registerFunction("unset")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinUnset, this);

    registerFunction("getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetVar, this);

    registerFunction("getvar.exitstatus")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetExitStatus, this);

    registerFunction("setvar.exitstatus")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSetExitStatus, this);

    registerFunction("getvar.processid")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetProcessId, this);

    registerFunction("getvar.backgroundid")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetBackgroundId, this);

    registerFunction("getvar.positional")
        .param<CoreVM::CoreNumber>("index")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetPositional, this);

    registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcess, this);

    registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcessShellPiped, this);

    registerFunction("read")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinReadDefault, this);

    registerFunction("read")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinRead, this);

    registerFunction("internal.open_read")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenRead, this);

    registerFunction("internal.open_write")
        .param<std::string>("path")
        .param<CoreVM::CoreNumber>("oflags")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenWrite, this);

    registerFunction("internal.cmd_start")
        .param<std::string>("program")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinCmdStart, this);

    registerFunction("internal.cmd_arg")
        .param<std::string>("arg")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinCmdArg, this);

    registerFunction("internal.cmd_exec")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExec, this);

    registerFunction("internal.cmd_exec_piped")
        .param<bool>("last_in_chain")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExecPiped, this);

    registerFunction("internal.redirect_start")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectStart, this);

    registerFunction("internal.redirect_input")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectInput, this);

    registerFunction("internal.redirect_output")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<std::string>("path")
        .param<bool>("append")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectOutput, this);

    registerFunction("internal.redirect_fd_dup")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<CoreVM::CoreNumber>("target_fd")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectFdDup, this);

    registerFunction("internal.redirect_heredoc")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectHeredoc, this);

    registerFunction("internal.redirect_herestring")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectHerestring, this);

    registerFunction("internal.redirect_end")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectEnd, this);

    registerFunction("internal.subst_start")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSubstStart, this);

    registerFunction("internal.subst_end")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinSubstEnd, this);

    registerFunction("internal.procsubst_fork")
        .param<bool>("is_write")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinProcSubstFork, this);

    registerFunction("internal.procsubst_exit")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinProcSubstExit, this);

    registerFunction("internal.procsubst_get_path")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinProcSubstGetPath, this);

    registerFunction("internal.procsubst_cleanup")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinProcSubstCleanup, this);

    registerFunction("expand.tilde")
        .param<std::string>("suffix")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandTilde, this);

    registerFunction("expand.tilde_user")
        .param<std::string>("user")
        .param<std::string>("suffix")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandTildeUser, this);

    registerFunction("expand.glob")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExpandGlob, this);

    registerFunction("expand.arith_to_string")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinArithToString, this);

    registerFunction("expand.arith_getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinArithGetVar, this);

    registerFunction("expand.arith_pow")
        .param<CoreVM::CoreNumber>("base")
        .param<CoreVM::CoreNumber>("exp")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinArithPow, this);

    registerFunction("expand.param_length")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamLength, this);

    registerFunction("expand.param_default")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamDefault, this);

    registerFunction("expand.param_alternate")
        .param<std::string>("var")
        .param<std::string>("alternate")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamAlternate, this);

    registerFunction("expand.param_assign")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamAssign, this);

    registerFunction("expand.param_error")
        .param<std::string>("var")
        .param<std::string>("error_msg")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamError, this);

    registerFunction("expand.param_remove_prefix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamRemovePrefix, this);

    registerFunction("expand.param_remove_suffix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamRemoveSuffix, this);

    registerFunction("expand.param_replace")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<std::string>("replacement")
        .param<bool>("all")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamReplace, this);

    registerFunction("internal.for_init")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForInit, this);

    registerFunction("internal.for_add_item")
        .param<std::string>("item")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForAddItem, this);

    registerFunction("internal.for_has_more")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinForHasMore, this);

    registerFunction("internal.for_next")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForNext, this);

    registerFunction("internal.for_cleanup")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForCleanup, this);

    registerFunction("internal.case_match")
        .param<std::string>("word")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinCaseMatch, this);

    registerFunction("internal.function_register")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinFunctionRegister, this);

    registerFunction("internal.function_call")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFunctionCall, this);

    // Job control builtins
    registerFunction("jobs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinJobs, this);

    registerFunction("fg")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFg, this);

    registerFunction("fg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFg, this);

    registerFunction("bg")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBg, this);

    registerFunction("bg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBg, this);

    registerFunction("wait")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWait, this);

    registerFunction("wait")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWait, this);

    registerFunction("internal.cmd_exec_piped_background")
        .param<std::string>("command")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExecPipedBackground, this);
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

        auto runner = CoreVM::Runner(handler, nullptr, &_globals, std::bind(&Shell::trace, this, _1, _2, _3));
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

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    auto const waitResult = _processManager.wait(spawnResult.value());
    if (!waitResult.has_value())
    {
        error("Failed to wait for {}: {}", program, toString(waitResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    _exitCode = waitResult->exitCode;
    if (waitResult->signaled)
        debugLog()()("child process exited with signal {}\n", waitResult->signal);
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

    if (lastInChain)
    {
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

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    auto const waitResult = _processManager.wait(spawnResult.value());
    if (!waitResult.has_value())
    {
        error("Failed to wait for {}: {}", program, toString(waitResult.error()));
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    _exitCode = waitResult->exitCode;
    if (waitResult->signaled)
        debugLog()()("child process exited with signal {}\n", waitResult->signal);
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

    if (lastInChain)
    {
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

void Shell::builtinTrue(CoreVM::Params& context)
{
    _exitCode = 0;
    context.setResult(true);
}

void Shell::builtinFalse(CoreVM::Params& context)
{
    _exitCode = 1;
    context.setResult(false);
}

void Shell::builtinReadDefault(CoreVM::Params& context)
{
    std::string const line =
        readLine(_tty, std::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m"));
    _env.set("REPLY", line);
    context.setResult(line);
}

void Shell::builtinRead(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);
    std::string const& variable = args.at(0);
    std::string const line =
        readLine(_tty, std::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m"));
    _env.set(variable, line);
    context.setResult(line);
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

    auto runner = CoreVM::Runner(handler, nullptr, &_globals, std::bind(&Shell::trace, this, _1, _2, _3));
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
    if (context.count() > 1)
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
    debugLog()()("trace: {}\n", CoreVM::disassemble(instr, ip, sp, &_currentProgram->constants()));
}

std::vector<std::string>& Shell::cmdBuilderArgs()
{
    if (_cmdBuilderStack.empty())
        _cmdBuilderStack.emplace_back();
    return _cmdBuilderStack.back();
}

} // namespace endo
