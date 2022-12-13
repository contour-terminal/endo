// SPDX-License-Identifier: Apache-2.0
#include <shell/ASTPrinter.h>
#include <shell/IRGenerator.h>
#include <shell/Parser.h>
#include <shell/Shell.h>

#include <CoreVM/Diagnostics.h>
#include <CoreVM/TargetCodeGenerator.h>
#include <CoreVM/ir/PassManager.h>
#include <CoreVM/transform/EmptyBlockElimination.h>
#include <CoreVM/transform/InstructionElimination.h>
#include <CoreVM/transform/MergeBlockPass.h>
#include <CoreVM/transform/UnusedBlockPass.h>
#include <CoreVM/vm/Instruction.h>
#include <CoreVM/vm/Program.h>

#include <crispy/utils.h>

#include <cstdio>
#include <iostream>

#include <sys/wait.h>

#include <fcntl.h>

namespace crush
{

// {{{ trace macros
// clang-format off
#if 0
    #define DEBUG(msg) do { fmt::print("{}\n", (msg)); } while (0)
    #define DEBUGF(msg, ...) do { fmt::print("{}\n", fmt::format((msg), __VA_ARGS__)); } while (0)
#else
    #define DEBUG(msg) do {} while (0)
    #define DEBUGF(msg, ...) do {} while (0)
#endif
// clang-format on
// }}}

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace
{
    std::vector<char const*> constructArgv(CoreVM::CoreStringArray const& args)
    {
        std::vector<char const*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg: args)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        return argv;
    }
} // namespace

// {{{ SystemEnvironment
SystemEnvironment& SystemEnvironment::instance()
{
    static SystemEnvironment env;
    return env;
}

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

void SystemEnvironment::exportVariable(std::string_view name)
{
    if (auto i = _values.find(name.data()); i != _values.end())
        setenv(name.data(), i->second.data(), 1);
}
// }}}
// {{{ TestEnvironment
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

void TestEnvironment::exportVariable(std::string_view name)
{
    if (auto i = _values.find(name.data()); i != _values.end())
        setenv(name.data(), i->second.data(), 1);
}
// }}}

Shell::Shell(): Shell(std::cin, std::cout, std::cerr, SystemEnvironment::instance())
{
}

Shell::Shell(std::istream& input, std::ostream& output, std::ostream& error, Environment& env):
    _env { env }, _input { input }, _output { output }, _error { error }
{
    // NB: These lines could go away once we have a proper command line parser and
    //     the ability to set these options from the command line.
    _optimize = _env.get("SHELL_IR_OPTIMIZE").value_or("0") != "0";
    _debugIR = _env.get("SHELL_IR_DEBUG").value_or("0") != "0";
    _traceVM = _env.get("SHELL_VM_TRACE").value_or("0") != "0";

    registerBuiltinFunctions();
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
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExport, this);

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

    registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        //.param<std::vector<CoreVM::CoreNumber>>("redirects")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcess, this);

    // used to redirect file to stdin
    registerFunction("internal.open_read")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenRead, this);

    // used for redirecting output to a file
    registerFunction("internal.open_write")
        .param<std::string>("path")
        .param<CoreVM::CoreNumber>("oflags")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenWrite, this);

    // used to remap a file descriptor to a specific number
    registerFunction("internal.dup2")
        .param<CoreVM::CoreNumber>("oldfd")
        .param<CoreVM::CoreNumber>("newfd")
        .bind(&Shell::builtinDup2, this);

    // used to create a pipe for connecting commands in a process group (call chain)
    registerFunction("internal.pipe")
        .param<CoreVM::CoreNumber>("fd")
        .returnType(CoreVM::LiteralType::IntPair)
        .bind(&Shell::builtinPipe, this);
    // clang-format on
}

void Shell::builtinDup2(CoreVM::Params& context)
{
    int const oldfd = static_cast<int>(context.getInt(1));
    int const newfd = static_cast<int>(context.getInt(2));
    int const result = dup2(oldfd, newfd);

    if (result == -1)
    {
        error("Failed to dup2({}, {}): {}", oldfd, newfd, strerror(errno));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    context.setResult(CoreVM::CoreNumber(result));
}

void Shell::builtinPipe(CoreVM::Params& context)
{
    int fds[2];
    int const result = pipe(fds);

    if (result != 0)
    {
        error("Failed to create pipe: {}", strerror(errno));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    // TODO: context.setResult(CoreVM::CoreIntPair(fds[0], fds[1]));
}

void Shell::builtinOpenRead(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);
    int const fd = open(path.data(), O_RDONLY);
    if (fd == -1)
    {
        error("Failed to open file '{}': {}", path, strerror(errno));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    context.setResult(CoreVM::CoreNumber(fd));
}

void Shell::builtinOpenWrite(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);
    int const oflags = static_cast<int>(context.getInt(2));
    int const fd = open(path.data(), oflags ? oflags : (O_WRONLY | O_CREAT | O_TRUNC));
    if (fd == -1)
    {
        error("Failed to open file '{}': {}", path, strerror(errno));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    context.setResult(CoreVM::CoreNumber(fd));
}

void Shell::builtinChDirHome(CoreVM::Params& context)
{
    char const* home = getenv("HOME");
    int const result = chdir(home);
    if (result != 0)
        error("Failed to change directory to '{}'", home);

    context.setResult(result == 0);
}

void Shell::builtinChDir(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);

    _env.set("OLDPWD", _env.get("PWD").value_or(""));
    _env.set("PWD", path);

    int const result = chdir(path.data());
    if (result != 0)
        error("Failed to change directory to '{}'", path);

    context.setResult(result == 0);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinExit(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
    _runner->suspend();
    _quit = true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinExport(CoreVM::Params& context)
{
    setenv(context.getString(1).data(), context.getString(2).data(), 1);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinTrue(CoreVM::Params& context)
{
    context.setResult(true);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinFalse(CoreVM::Params& context)
{
    context.setResult(false);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::optional<std::filesystem::path> Shell::resolveProgram(std::string const& program) const
{
    auto const pathEnv = _env.get("PATH");
    if (!pathEnv.has_value())
        return std::nullopt;

    auto const pathEnvValue = pathEnv.value();
    auto const paths = crispy::split(pathEnvValue, ':');

    for (auto const& pathStr: paths)
    {
        auto path = std::filesystem::path(pathStr);
        auto const programPath = path / program;
        if (std::filesystem::exists(programPath))
        {
            DEBUGF("Found program: {}", programPath.string());
            return programPath;
        }
    }

    return std::nullopt;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinCallProcess(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);
    std::vector<char const*> const argv = constructArgv(args);
    std::string const& program = args.at(0);
    std::optional<std::filesystem::path> const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("Failed to resolve program '{}'", program);
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    // TODO: setup redirects
    // CoreVM::CoreIntArray const& outputRedirects = context.getIntArray(1);
    // for (size_t i = 0; i + 2 < outputRedirects.size(); i += 2)
    //     DEBUGF("redirect: {} -> {}\n", outputRedirects[i], outputRedirects[i + 1]);

    pid_t const pid = fork();
    switch (pid)
    {
        case -1:
            error("Failed to fork(): {}", strerror(errno));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        case 0: {
            // child process
            execvp(programPath->c_str(), const_cast<char* const*>(argv.data()));
            error("Failed to execve({}): {}", programPath->string(), strerror(errno));
            _exit(EXIT_FAILURE);
        }
        default:
            // parent process
            int wstatus = 0;
            waitpid(pid, &wstatus, 0);
            if (WIFSIGNALED(wstatus))
                error("child process exited with signal {}", WTERMSIG(wstatus));
            else if (WIFEXITED(wstatus))
                error("child process exited with code {}", _exitCode = WEXITSTATUS(wstatus));
            else if (WIFSTOPPED(wstatus))
                error("child process stopped with signal {}", WSTOPSIG(wstatus));
            else
                error("child process exited with unknown status {}", wstatus);
            break;
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

int Shell::run()
{
    while (!_quit && prompt.ready())
    {
        auto const lineBuffer = prompt.read();
        DEBUGF("input buffer: {}", lineBuffer);

        _exitCode = execute(lineBuffer);
        _error << fmt::format("exit code: {}\n", _exitCode);
    }

    return _quit ? _exitCode : EXIT_SUCCESS;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::trace(CoreVM::Instruction instr, size_t ip, size_t sp)
{
    if (_traceVM)
        _error << fmt::format("trace: {}\n",
                              CoreVM::disassemble(instr, ip, sp, &_currentProgram->constants()));
}

int Shell::execute(std::string const& lineBuffer) // NOLINT
{
    try
    {
        CoreVM::diagnostics::ConsoleReport report;
        auto parser = crush::Parser(*this, report, std::make_unique<crush::StringSource>(lineBuffer));
        auto const rootNode = parser.parse();
        if (!rootNode)
        {
            error("Failed to parse input");
            return EXIT_FAILURE;
        }

        DEBUGF("Parsed & printed: {}", crush::ast::ASTPrinter::print(*rootNode));

        CoreVM::IRProgram* irProgram = IRGenerator::generate(*rootNode);
        if (irProgram == nullptr)
        {
            error("Failed to generate IR program");
            return EXIT_FAILURE;
        }

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

            pm.run(irProgram);
        }
        if (_debugIR)
        {
            DEBUG("================================================\n");
            DEBUG("Optimized IR program:\n");
            irProgram->dump();
        }

        _currentProgram = CoreVM::TargetCodeGenerator {}.generate(irProgram);
        if (!_currentProgram)
        {
            error("Failed to generate target code");
            return EXIT_FAILURE;
        }
        _currentProgram->link(this, &report);

        if (_debugIR)
        {
            DEBUG("================================================\n");
            DEBUG("Linked target code:\n");
            _currentProgram->dump();
        }

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

} // namespace crush
