// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file DebugSession.hpp
/// @brief Owns the compiled program, runner, breakpoint manager, and stop state for a DAP debug session.

#include <CoreVM/vm/Program.hpp>
#include <CoreVM/vm/Runner.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "BreakpointManager.hpp"
#include "DapTypes.hpp"
#include <nlohmann/json.hpp>

namespace endo::dap
{

/// Reason the VM is stopped.
enum class StopReason : uint8_t
{
    None,
    Breakpoint,
    Entry,
    Step,
    Pause,
    Exception,
};

/// Stepping mode for execution control.
enum class StepMode : uint8_t
{
    None,
    StepOver,
    StepIn,
    StepOut,
};

/// Stepping granularity controls whether steps stop per source line or per instruction.
enum class SteppingGranularity : uint8_t
{
    Line,
    Instruction,
};

/// Callback type for sending DAP events.
using EventSender = std::function<void(std::string const& event, nlohmann::json body)>;

/// Manages one debug session: owns the compiled Program, Runner, BreakpointManager, and stop state.
///
/// Provides the DAP-aware TraceLogger callback that suspends the VM on breakpoint hits.
class DebugSession
{
  public:
    /// Constructs a debug session.
    /// @param program Compiled program (ownership transferred)
    /// @param launchArgs Launch configuration
    /// @param eventSender Callback for emitting DAP events
    DebugSession(std::unique_ptr<CoreVM::Program> program,
                 LaunchRequestArguments launchArgs,
                 EventSender eventSender);

    /// Starts execution from the beginning.
    /// @return true if execution completed, false if VM suspended (breakpoint hit)
    bool startExecution();

    /// Continues execution after a stop (clears all step state).
    /// @return true if execution completed, false if VM suspended again
    bool continueExecution();

    /// Resumes execution without clearing step state (used by stepping commands).
    /// @return true if execution completed, false if VM suspended again
    bool resumeExecution();

    /// Returns true if the VM is currently stopped (suspended).
    [[nodiscard]] bool isStopped() const noexcept;

    /// Returns true if execution has completed.
    [[nodiscard]] bool isTerminated() const noexcept;

    /// Access the breakpoint manager for setting/querying breakpoints.
    [[nodiscard]] BreakpointManager& breakpointManager() noexcept { return _breakpointManager; }

    /// Access the compiled program for breakpoint resolution.
    [[nodiscard]] CoreVM::Program const& program() const noexcept { return *_program; }

    /// Returns the stop reason for the current stop.
    [[nodiscard]] StopReason stopReason() const noexcept { return _stopReason; }

    /// Returns the breakpoint IDs that caused the current stop.
    [[nodiscard]] std::vector<int> const& hitBreakpointIds() const noexcept { return _hitBreakpointIds; }

    /// Sets the stepping mode and records current execution position.
    void setStepMode(StepMode mode, SteppingGranularity granularity = SteppingGranularity::Line);

    /// Requests an asynchronous pause at the next instruction.
    void requestPause();

    /// Access the runner for inspection (may be null).
    [[nodiscard]] CoreVM::Runner const* runner() const noexcept { return _runner.get(); }

    /// Returns the current stack trace.
    /// @param startFrame Index of the first frame to return (0-based)
    /// @param levels Maximum number of frames to return (0 = all)
    [[nodiscard]] std::vector<StackFrame> getStackTrace(int startFrame, int levels) const;

    /// Returns scopes for a given frame ID.
    [[nodiscard]] std::vector<Scope> getScopes(int frameId) const;

    /// Returns variables for a given variables reference.
    [[nodiscard]] std::vector<Variable> getVariables(int variablesReference) const;

    /// Evaluates an expression (variable name lookup, arithmetic, comparisons).
    /// @param expression Expression string
    /// @param frameId Stack frame for variable context
    /// @param context Evaluation context ("hover", "watch", "repl")
    [[nodiscard]] std::optional<EvaluateResult> evaluate(std::string const& expression,
                                                         int frameId,
                                                         std::string const& context = "hover") const;

    /// Sets the exception breakpoint filters (e.g., "runtime-error", "all").
    void setExceptionFilters(std::vector<std::string> const& filters);

    /// Returns info about the current exception (if stopped due to exception).
    [[nodiscard]] std::optional<CoreVM::RuntimeError> getExceptionInfo() const;

    /// Sets a variable's value in the current scope.
    /// @param variablesReference Scope reference
    /// @param name Variable name
    /// @param value New value as string
    /// @return Updated variable, or nullopt on failure
    [[nodiscard]] std::optional<Variable> setVariable(int variablesReference,
                                                      std::string const& name,
                                                      std::string const& value);

    /// Returns disassembled instructions for a memory reference.
    /// @param memoryReference Hex-encoded address: "0x{funcIndex:8}{ip:8}"
    /// @param instructionCount Number of instructions to return
    /// @param instructionOffset Offset from the reference
    [[nodiscard]] std::vector<nlohmann::json> disassemble(std::string const& memoryReference,
                                                          int instructionCount,
                                                          int instructionOffset) const;

    /// Returns paths of all source files loaded for the program.
    [[nodiscard]] std::vector<std::string> const& loadedSources() const noexcept { return _loadedSources; }

    /// Returns the content of a loaded source file, or nullopt if not found.
    /// @param path Absolute path to the source file (must be in loadedSources)
    [[nodiscard]] std::optional<std::string> getSourceContent(std::string const& path) const;

    /// Returns completion items for the debug console (variables + function names).
    /// @param text Prefix text to filter completions
    /// @param frameId Stack frame context
    [[nodiscard]] std::vector<CompletionItem> getCompletions(std::string const& text, int frameId) const;

  private:
    /// TraceLogger callback invoked before each VM instruction.
    void onTrace(CoreVM::Instruction instr, size_t ip, size_t sp);

    /// Suspends the VM and emits a stopped event with the given reason.
    void stopExecution(StopReason reason, size_t ip);

    /// Handles completion or suspension after runner execution.
    /// @return true if completed, false if suspended
    bool handleRunResult(bool exitNonZero);

    /// Handles a RunResult (with error info) for exception breakpoints.
    /// @return true if completed, false if suspended
    bool handleRunResultWithError(CoreVM::Runner::RunResult result);

    std::unique_ptr<CoreVM::Program> _program;
    LaunchRequestArguments _launchArgs;
    EventSender _eventSender;

    BreakpointManager _breakpointManager;

    CoreVM::Runner::Globals _globals;
    std::unique_ptr<CoreVM::Runner> _runner;

    StopReason _stopReason = StopReason::None;
    std::vector<int> _hitBreakpointIds;

    // Track last stop location to avoid re-stopping on multi-instruction lines
    std::string _lastStopFile;
    int _lastStopLine = -1;

    // Stepping state
    StepMode _stepMode = StepMode::None;
    size_t _stepStartDepth = 0;
    int _stepStartLine = -1;
    std::string _stepStartFile;
    bool _pauseRequested = false;
    bool _stopOnEntry = false;
    SteppingGranularity _steppingGranularity = SteppingGranularity::Line;

    // Exception breakpoints
    std::set<std::string> _exceptionFilters;
    std::optional<CoreVM::RuntimeError> _currentException;

    bool _terminated = false;

    // Loaded source files (populated from function location tables)
    std::vector<std::string> _loadedSources;

    // Variable expansion references (mutable because getVariables/evaluate are const)
    mutable int _nextVarRef = 100000;
    mutable std::unordered_map<int, std::pair<uint64_t, CoreVM::TypedObject*>> _varRefMap;

    /// Allocates a new variable reference for the given raw value and object.
    int allocateVarRef(uint64_t rawValue, CoreVM::TypedObject* obj) const;

    /// Resets variable reference map (called on stop/resume).
    void resetVarRefMap() const;

    /// Returns true if the object supports expansion (has children).
    static bool isExpandable(CoreVM::TypedObject const* obj);

    /// Enumerates the children of an expanded variable reference.
    std::vector<Variable> enumerateChildren(int varRef) const;

    /// If a child slot value is an expandable object, allocate a sub-reference.
    int maybeAllocateChildRef(uint64_t rawValue) const;
};

} // namespace endo::dap
