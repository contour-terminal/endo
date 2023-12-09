// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ProcessGroup.h>
#include <shell/Prompt.h>
#include <shell/TTY.h>

#include <CoreVM/NativeCallback.h>
#include <CoreVM/Params.h>
#include <CoreVM/vm/Runtime.h>

#include <fmt/format.h>

#include <filesystem>

#include <sys/ioctl.h>

#include <fcntl.h>
#include <unistd.h>

import UnixPipe;

namespace endo
{

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

class TestEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void exportVariable(std::string_view name) override;

  private:
    std::map<std::string, std::string> _values;
};

class SystemEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void exportVariable(std::string_view name) override;

    static SystemEnvironment& instance();

  private:
    std::map<std::string, std::string> _values;
};

class Shell final: public CoreVM::Runtime
{
  public:
    Shell();
    Shell(TTY& tty, Environment& env);

    [[nodiscard]] Environment& environment() noexcept { return _env; }
    [[nodiscard]] Environment const& environment() const noexcept { return _env; }

    void setOptimize(bool optimize) { _optimize = optimize; }

    int run();
    int execute(std::string const& lineBuffer);

    Prompt prompt;
    std::vector<ProcessGroup> processGroups;

  private:
    void registerBuiltinFunctions();

    // builtins that match to shell commands
    void builtinExit(CoreVM::Params& context);
    void builtinCallProcess(CoreVM::Params& context);
    void builtinCallProcessShellPiped(CoreVM::Params& context);
    void builtinChDirHome(CoreVM::Params& context);
    void builtinChDir(CoreVM::Params& context);
    void builtinSet(CoreVM::Params& context);
    void builtinSetAndExport(CoreVM::Params& context);
    void builtinExport(CoreVM::Params& context);
    void builtinTrue(CoreVM::Params& context);
    void builtinFalse(CoreVM::Params& context);
    void builtinReadDefault(CoreVM::Params& context);
    void builtinRead(CoreVM::Params& context);

    // helper-builtins for redirects and pipes
    void builtinOpenRead(CoreVM::Params& context);
    void builtinOpenWrite(CoreVM::Params& context);

    [[nodiscard]] std::optional<std::filesystem::path> resolveProgram(std::string const& program) const;

    void trace(CoreVM::Instruction instr, size_t ip, size_t sp);

    template <typename... Args>
    void error(fmt::format_string<Args...> const& message, Args&&... args)
    {
        std::cerr << fmt::format(message, std::forward<Args>(args)...) + "\n";
    }

  private:
    Environment& _env;

    TTY& _tty;

    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    bool _debugIR = false;
    bool _traceVM = false;
    bool _optimize = false;

    PipelineBuilder _currentPipelineBuilder;

    // This stores the PIDs of all processes in the pipeline's process group.
    std::vector<pid_t> _currentProcessGroupPids;
    std::optional<pid_t> _leftPid;
    std::optional<pid_t> _rightPid;

    // This stores the exit code of the last process in the pipeline.
    // TODO: remember exit codes from all processes in the pipeline's process group
    int _exitCode = -1;

    CoreVM::Runner* _runner = nullptr;
    bool _quit = false;
};

} // namespace endo
