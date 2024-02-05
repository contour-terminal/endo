// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/ProcessGroup.h>

#include <crispy/App.h>
#include <crispy/assert.h>
#include <crispy/utils.h>

#include <cstdio>
#include <iostream>
#include <map>

#include <sys/wait.h>

#include <fcntl.h>

import TTY;
import UnixPipe;
import Prompt;
import Lexer;
import ASTPrinter;
import IRGenerator;
import Parser;
import CoreVM;

#if defined(ENDO_USE_LLVM)
import LLVMBackend;
#endif

export module Shell;

auto inline debugLog = logstore::category("debug ", "Debug log", logstore::category::state::Enabled);

namespace endo
{

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

template <typename T>
std::vector<char const*> constructArgv(T const& args)
{
    std::vector<char const*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg: args)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    return argv;
}

std::string readLine(TTY& tty, std::string_view prompt)
{
    // Most super-native implementation, yet to be replaced by a proper line editor.
    tty.writeToStdout(fmt::format("{}", prompt));
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

class Environment
{
  public:
    virtual ~Environment() = default;

    virtual void set(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual std::optional<std::string_view> get(std::string_view name) const = 0;

    virtual void exportVariable(std::string_view name) = 0;

    void setAndExport(std::string_view name, std::string_view value)
    {
        set(name, value);
        exportVariable(name);
    }
};

struct PipelineBuilder
{
    struct IODescriptors
    {
        int reader;
        int writer;
    };

    int defaultStdinFd = STDIN_FILENO;
    int defaultStdoutFd = STDOUT_FILENO;
    std::optional<UnixPipe> currentPipe = std::nullopt;

    auto requestShellPipe(bool lastInChain) -> IODescriptors;
};

inline auto PipelineBuilder::requestShellPipe(bool lastInChain) -> IODescriptors
{
    int const stdinFd = !currentPipe ? defaultStdinFd : currentPipe->releaseReader();
    currentPipe = lastInChain ? std::nullopt : std::make_optional<UnixPipe>();
    int const stdoutFd = lastInChain ? defaultStdoutFd : currentPipe->writer();
    return IODescriptors { .reader = stdinFd, .writer = stdoutFd };
}

export class TestEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override
    {
        _values[std::string(name)] = std::string(value);
    }
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            return i->second;
        else if (auto const* value = getenv(name.data()))
            return std::string_view { value };
        else
            return std::nullopt;
    }
    void exportVariable(std::string_view name) override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            setenv(name.data(), i->second.data(), 1);
    }

  private:
    std::map<std::string, std::string> _values;
};

export class SystemEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override
    {
        _values[std::string(name)] = std::string(value);
    }
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            return i->second;
        else if (auto const* value = getenv(name.data()))
            return std::string_view { value };
        else
            return std::nullopt;
    }
    void exportVariable(std::string_view name) override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            setenv(name.data(), i->second.data(), 1);
    }

    static SystemEnvironment& instance()
    {
        static SystemEnvironment env;
        return env;
    }

  private:
    std::map<std::string, std::string> _values;
};

class ShellBase: public crispy::app
{
  public:
    ShellBase():
        app("endo", "Endo shell", ENDO_VERSION_STRING, "Apache-2.0"),
        _env { SystemEnvironment::instance() },
        _tty { RealTTY::instance() }
    {
    }
    ShellBase(TTY& tty, Environment& env):
        app("endo", "Endo shell", ENDO_VERSION_STRING, "Apache-2.0"), _env { env }, _tty { tty }
    {

        _currentPipelineBuilder.defaultStdinFd = _tty.inputFd();
        _currentPipelineBuilder.defaultStdoutFd = _tty.outputFd();

        _env.setAndExport("SHELL", "endo");
    }
    virtual ~ShellBase() = default;
    [[nodiscard]] crispy::cli::command parameterDefinition() const override
    {

        return crispy::cli::command {
            "endo",
            "Endo Terminal " ENDO_VERSION_STRING
            " - a shell for the 21st century",
            crispy::cli::option_list {},
            crispy::cli::command_list {},
        };
    }

    virtual int execute(std::string const& lineBuffer) = 0;
    int run(int argc, char const* argv[])
    {
        try
        {
            customizeLogStoreOutput();

            while (!_quit && prompt.ready())
            {
                auto const lineBuffer = prompt.read();
                debugLog()("input buffer: {}", lineBuffer);

                _exitCode = execute(lineBuffer);
                // _tty.writeToStdout("exit code: {}\n", _exitCode);
            }

            return _quit ? _exitCode : EXIT_SUCCESS;
        }
        catch (std::exception const& e)
        {
            std::cerr << fmt::format("Unhandled error caught. {}", e.what()) << '\n';
            return EXIT_FAILURE;
        }
    }
    [[nodiscard]] Environment& environment() noexcept { return _env; }
    [[nodiscard]] Environment const& environment() const noexcept { return _env; }

    void setOptimize(bool optimize) { _optimize = optimize; }

    template <typename... Args>
    void error(fmt::format_string<Args...> const& message, Args&&... args)
    {
        std::cerr << fmt::format(message, std::forward<Args>(args)...) + "\n";
    }

    [[nodiscard]] std::optional<std::filesystem::path> resolveProgram(std::string const& program) const
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
                debugLog()("Found program: {}", programPath.string());
                return programPath;
            }
        }

        return std::nullopt;
    }

    void builtinCallProcess(std::string const& program, std::vector<std::string> const& args)
    {
        std::vector<char const*> const argv = constructArgv(args);
        std::optional<std::filesystem::path> const programPath = resolveProgram(program);

        int const stdinFd = _currentPipelineBuilder.defaultStdinFd;
        int const stdoutFd = _currentPipelineBuilder.defaultStdoutFd;

        if (!programPath.has_value())
        {
            error("Failed to resolve program '{}'", program);
            return;
        }

        // TODO: setup redirects
        // CoreVM::CoreIntArray const& outputRedirects = context.getIntArray(1);
        // for (size_t i = 0; i + 2 < outputRedirects.size(); i += 2)
        //     debugLog()("redirect: {} -> {}\n", outputRedirects[i], outputRedirects[i + 1]);

        pid_t const pid = fork();
        switch (pid)
        {
            case -1: error("Failed to fork(): {}", strerror(errno)); return;
            case 0: {
                // child process
                if (stdinFd != STDIN_FILENO)
                    dup2(stdinFd, STDIN_FILENO);
                if (stdoutFd != STDOUT_FILENO)
                    dup2(stdoutFd, STDOUT_FILENO);
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
    }

  public:
    Prompt prompt;
    std::vector<ProcessGroup> processGroups;

  protected:
    Environment& _env;

    TTY& _tty;

    PipelineBuilder _currentPipelineBuilder;

    // This stores the PIDs of all processes in the pipeline's process group.
    std::vector<pid_t> _currentProcessGroupPids;
    std::optional<pid_t> _leftPid;
    std::optional<pid_t> _rightPid;

    // This stores the exit code of the last process in the pipeline.
    // TODO: remember exit codes from all processes in the pipeline's process group
    int _exitCode = -1;

    bool _optimize = false;

    bool _quit = false;
};

export class ShellLLVM final: public ShellBase
{
  public:
    ShellLLVM(): ShellLLVM(RealTTY::instance(), SystemEnvironment::instance()) {}

    ShellLLVM(TTY& tty, Environment& env): ShellBase { tty, env }, _backend {} {}

    int execute(std::string const& lineBuffer) override;

  private:
    LLVMBackend _backend;
};

export class ShellCoreVM final: public CoreVM::Runtime, public ShellBase
{
  public:
    ShellCoreVM(): ShellCoreVM(RealTTY::instance(), SystemEnvironment::instance()) {}

    ShellCoreVM(TTY& tty, Environment& env): ShellBase { tty, env }
    {
        // NB: These lines could go away once we have a proper command line parser and
        //     the ability to set these options from the command line.
        registerBuiltinFunctions();

        // for (CoreVM::NativeCallback const* callback: builtins())
        //      fmt::print("builtin: {}\n", callback->signature().to_s());
    }

    int execute(std::string const& lineBuffer) override;

    void trace(CoreVM::Instruction instr, size_t ip, size_t sp)
    {
        debugLog()(
            fmt::format("trace: {}\n", CoreVM::disassemble(instr, ip, sp, &_currentProgram->constants())));
    }

  private:
    void registerBuiltinFunctions();
    // builtins that match to shell commands
    void builtinExit(CoreVM::Params& context);
    void builtinCallProcess(CoreVM::Params& context);
    void builtinCallProcessShellPiped(CoreVM::Params& context);
    void builtinChDir(CoreVM::Params& context);
    void builtinChDirHome(CoreVM::Params& context);
    void builtinSet(CoreVM::Params& context);
    void builtinSetAndExport(CoreVM::Params& context);
    void builtinExport(CoreVM::Params& context) { _env.exportVariable(context.getString(1)); }
    void builtinTrue(CoreVM::Params& context) { context.setResult(true); }
    void builtinFalse(CoreVM::Params& context) { context.setResult(false); }
    void builtinReadDefault(CoreVM::Params& context);
    void builtinRead(CoreVM::Params& context);
    // helper-builtins for redirects and pipes
    void builtinOpenRead(CoreVM::Params& context);
    void builtinOpenWrite(CoreVM::Params& context);

  private:
    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    CoreVM::Runner* _runner = nullptr;
};

void ShellCoreVM::builtinOpenWrite(CoreVM::Params& context)
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

void ShellCoreVM::builtinOpenRead(CoreVM::Params& context)
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

void ShellCoreVM::builtinRead(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);
    std::string const& variable = args.at(0);
    std::string const line =
        readLine(_tty, fmt::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m"));
    _env.set(variable, line);
    context.setResult(line);
}

void ShellCoreVM::builtinReadDefault(CoreVM::Params& context)
{
    std::string const line =
        readLine(_tty, fmt::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m"));
    _env.set("REPLY", line);
    context.setResult(line);
}

void ShellCoreVM::builtinSetAndExport(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    _env.exportVariable(context.getString(1));
}

void ShellCoreVM::builtinSet(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    context.setResult(true);
}

void ShellCoreVM::builtinExit(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
    _runner->suspend();
    _quit = true;
}

void ShellCoreVM::builtinChDirHome(CoreVM::Params& context)
{
    auto const path = _env.get("HOME").value_or("/");
    _env.set("OLDPWD", std::filesystem::current_path().string());
    _env.set("PWD", path);
    int const result = chdir(path.data());
    if (result != 0)
        error("Failed to change directory to '{}'", path);

    context.setResult(result == 0);
}

void ShellCoreVM::builtinChDir(CoreVM::Params& context)
{
    std::string const& path = context.getString(1);

    _env.set("OLDPWD", _env.get("PWD").value_or(""));
    _env.set("PWD", path);

    int const result = chdir(path.data());
    if (result != 0)
        error("Failed to change directory to '{}'", path);

    context.setResult(result == 0);
}

void ShellCoreVM::builtinCallProcessShellPiped(CoreVM::Params& context)
{
    bool const lastInChain = context.getBool(1);
    CoreVM::CoreStringArray const& args = context.getStringArray(2);

    std::vector<char const*> const argv = constructArgv(args);
    std::string const& program = args.at(0);
    std::optional<std::filesystem::path> const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("Failed to resolve program '{}'", program);
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

    // TODO: setup redirects
    // CoreVM::CoreIntArray const& outputRedirects = context.getIntArray(1);
    // for (size_t i = 0; i + 2 < outputRedirects.size(); i += 2)
    //     debugLog()("redirect: {} -> {}\n", outputRedirects[i], outputRedirects[i + 1]);

    pid_t const pid = fork();
    switch (pid)
    {
        case -1:
            error("Failed to fork(): {}", strerror(errno));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        case 0: {
            // child process
            setpgid(0, !_currentProcessGroupPids.empty() ? _currentProcessGroupPids.front() : 0);
            if (stdinFd != STDIN_FILENO)
                dup2(stdinFd, STDIN_FILENO);
            if (stdoutFd != STDOUT_FILENO)
                dup2(stdoutFd, STDOUT_FILENO);
            execvp(programPath->c_str(), const_cast<char* const*>(argv.data()));
            error("Failed to execve({}): {}", programPath->string(), strerror(errno));
            _exit(EXIT_FAILURE);
        }
        default:
            // parent process
            _leftPid = _rightPid;
            _rightPid = pid;
            _currentProcessGroupPids.push_back(pid);
            if (lastInChain)
            {
                // This is the last process in the chain, so we need to wait for all
                for (pid_t const pid: _currentProcessGroupPids)
                {
                    int wstatus = 0;
                    waitpid(pid, &wstatus, 0);
                    if (WIFSIGNALED(wstatus))
                        error("child process {}, exited with signal {}", pid, WTERMSIG(wstatus));
                    else if (WIFEXITED(wstatus))
                        error("child process {} exited with code {}", pid, _exitCode = WEXITSTATUS(wstatus));
                    else if (WIFSTOPPED(wstatus))
                        error("child process {} stopped with signal {}", pid, WSTOPSIG(wstatus));
                    else
                        error("child process {} exited with unknown status {}", pid, wstatus);
                }
                _currentProcessGroupPids.clear();
                _leftPid = std::nullopt;
                _rightPid = std::nullopt;
            }
            break;
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void ShellCoreVM::builtinCallProcess(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);
    std::string const& program = args.at(0);
    ShellBase::builtinCallProcess(program, args);
}

void ShellCoreVM::registerBuiltinFunctions()
{
    // clang-format off
    registerFunction("exit")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&ShellCoreVM::builtinExit, this);

    registerFunction("export")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&ShellCoreVM::builtinExport, this);

    registerFunction("export")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&ShellCoreVM::builtinSetAndExport, this);

    registerFunction("true")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&ShellCoreVM::builtinTrue, this);

    registerFunction("false")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&ShellCoreVM::builtinFalse, this);

    registerFunction("cd")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&ShellCoreVM::builtinChDirHome, this);

    registerFunction("cd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&ShellCoreVM::builtinChDir, this);

    registerFunction("set")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&ShellCoreVM::builtinSet, this);

    registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        //.param<std::vector<CoreVM::CoreNumber>>("redirects")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&ShellCoreVM::builtinCallProcess, this);

    registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        //.param<std::vector<CoreVM::CoreNumber>>("redirects")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&ShellCoreVM::builtinCallProcessShellPiped, this);

    registerFunction("read")
        .returnType(CoreVM::LiteralType::String)
        .bind(&ShellCoreVM::builtinReadDefault, this);

    registerFunction("read")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::String)
        .bind(&ShellCoreVM::builtinRead, this);

    // used to redirect file to stdin
    registerFunction("internal.open_read")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&ShellCoreVM::builtinOpenRead, this);

    // used for redirecting output to a file
    registerFunction("internal.open_write")
        .param<std::string>("path")
        .param<CoreVM::CoreNumber>("oflags")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&ShellCoreVM::builtinOpenWrite, this);
    // clang-format on
}

int ShellCoreVM::execute(std::string const& lineBuffer)
{
    try
    {
        CoreVM::diagnostics::ConsoleReport report;
        auto parser = endo::Parser(*this, report, std::make_unique<endo::StringSource>(lineBuffer));
        auto const rootNode = parser.parse();
        if (!rootNode)
        {
            error("Failed to parse input");
            return EXIT_FAILURE;
        }
        debugLog()("Parsed & printed: {}", endo::ast::ASTPrinter::print(*rootNode));

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

        debugLog()("================================================\n");
        debugLog()("Optimized IR program:\n");
        if (debugLog.is_enabled())
            irProgram->dump();

        _currentProgram = CoreVM::TargetCodeGenerator {}.generate(irProgram);
        if (!_currentProgram)
        {
            error("Failed to generate target code");
            return EXIT_FAILURE;
        }
        _currentProgram->link(this, &report);

        debugLog()("================================================\n");
        debugLog()("Linked target code:\n");
        if (debugLog.is_enabled())
            _currentProgram->dump();

        CoreVM::Handler* main = _currentProgram->findHandler("@main");
        assert(main != nullptr);
        auto runner =
            CoreVM::Runner(main, nullptr, &_globals, std::bind(&ShellCoreVM::trace, this, _1, _2, _3));
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

int ShellLLVM::execute(std::string const& lineBuffer)
{
    try
    {
        auto tokens = Lexer::tokenize(std::make_unique<endo::StringSource>(lineBuffer));
        for (auto const& tokenInfo: tokens)
        {
            if (tokenInfo.token == Token::Identifier)
                if (tokenInfo.literal == "exit")
                {
                    _quit = true;
                    return EXIT_SUCCESS;
                }
            debugLog()("token: {}\n", tokenInfo.literal);
        }

        // Look up the JIT'd function, cast it to a function pointer, then call it.
        _backend.exec(
            lineBuffer, _currentPipelineBuilder.defaultStdinFd, _currentPipelineBuilder.defaultStdoutFd);
        return _exitCode;
    }
    catch (std::exception const& e)
    {
        error("Exception caught: {}", e.what());
        return EXIT_SUCCESS;
    }

    return EXIT_SUCCESS;
}

} // namespace endo
