// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ProcessGroup.h>
#include <shell/Prompt.h>

#include <CoreVM/NativeCallback.h>
#include <CoreVM/Params.h>
#include <CoreVM/vm/Runtime.h>

#include <filesystem>

#include <fmt/format.h>

namespace crush
{

class Environment
{
  public:
    virtual ~Environment() = default;

    virtual void set(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual std::optional<std::string_view> get(std::string_view name) const = 0;

    virtual void exportVariable(std::string_view name) = 0;
};

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
    Shell(std::istream& input, std::ostream& output, std::ostream& error, Environment& env);

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
    void builtinChDirHome(CoreVM::Params& context);
    void builtinChDir(CoreVM::Params& context);
    void builtinExport(CoreVM::Params& context);
    void builtinTrue(CoreVM::Params& context);
    void builtinFalse(CoreVM::Params& context);

    // helper-builtins for redirects and pipes
    void builtinPipe(CoreVM::Params& context);
    void builtinDup2(CoreVM::Params& context);
    void builtinOpenRead(CoreVM::Params& context);
    void builtinOpenWrite(CoreVM::Params& context);

    [[nodiscard]] std::optional<std::filesystem::path> resolveProgram(std::string const& program) const;

    void trace(CoreVM::Instruction instr, size_t ip, size_t sp);

    template <typename... Args>
    void error(fmt::format_string<Args...> const& message, Args&&... args)
    {
        _error << fmt::format(message, std::forward<Args>(args)...) << std::endl;
    }

  private:
    Environment& _env;
    std::istream& _input;
    std::ostream& _output;
    std::ostream& _error;

    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    bool _debugIR = false;
    bool _traceVM = false;
    bool _optimize = false;

    CoreVM::Runner* _runner = nullptr;
    bool _quit = false;
    int _exitCode = -1;
};

} // namespace crush
