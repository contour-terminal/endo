// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file DapServer.hpp
/// @brief DAP server implementing the Debug Adapter Protocol for Endo scripts.
///
/// Communicates via DAP messages over stdin/stdout (or injected streams for testing).
/// Supports the initialize/launch/configurationDone/disconnect lifecycle,
/// breakpoints, and continue.

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/Diagnostics.hpp>

#include <fstream>
#include <istream>
#include <memory>
#include <ostream>
#include <string>

#include "DapTypes.hpp"

namespace endo::dap
{

class DebugSession;

/// DAP server implementing the Debug Adapter Protocol for Endo shell scripts.
///
/// Communicates via DAP messages over stdin/stdout (or injected streams for testing).
/// Supports script launch, execution, breakpoints, and session lifecycle.
class DapServer
{
  public:
    /// Constructs the server with the given I/O streams.
    /// @param input Input stream for reading DAP messages (default: std::cin)
    /// @param output Output stream for writing DAP messages (default: std::cout)
    explicit DapServer(std::istream& input, std::ostream& output);

    /// Default constructor using std::cin and std::cout.
    DapServer();

    /// Destructor (defined in .cpp to complete DebugSession type).
    ~DapServer();

    /// Runs the main message loop until disconnect.
    /// @return 0 on clean exit
    int run();

    /// Enables protocol logging to the given file path (append mode).
    /// @param path File path for the log output
    void setLogFile(std::string const& path);

  private:
    // Message construction (DAP format)
    void sendResponse(int requestSeq, std::string const& command, nlohmann::json body = {});
    void sendErrorResponse(int requestSeq, std::string const& command, std::string const& message);
    void sendEvent(std::string const& event, nlohmann::json body = {});

    // Message dispatch
    void dispatch(nlohmann::json const& message);

    // Request handlers
    nlohmann::json handleInitialize(nlohmann::json const& args);
    void handleConfigurationDone(int requestSeq);
    void handleLaunch(int requestSeq, nlohmann::json const& args);
    void handleDisconnect(int requestSeq, nlohmann::json const& args);
    void handleTerminate(int requestSeq);
    void handleSetBreakpoints(int requestSeq, nlohmann::json const& args);
    void handleSetFunctionBreakpoints(int requestSeq, nlohmann::json const& args);
    void handleBreakpointLocations(int requestSeq, nlohmann::json const& args);
    void handleContinue(int requestSeq, nlohmann::json const& args);
    void handleNext(int requestSeq, nlohmann::json const& args);
    void handleStepIn(int requestSeq, nlohmann::json const& args);
    void handleStepOut(int requestSeq, nlohmann::json const& args);
    void handlePause(int requestSeq, nlohmann::json const& args);
    void handleThreads(int requestSeq);
    void handleStackTrace(int requestSeq, nlohmann::json const& args);
    void handleScopes(int requestSeq, nlohmann::json const& args);
    void handleVariables(int requestSeq, nlohmann::json const& args);
    void handleEvaluate(int requestSeq, nlohmann::json const& args);
    void handleSetExceptionBreakpoints(int requestSeq, nlohmann::json const& args);
    void handleExceptionInfo(int requestSeq, nlohmann::json const& args);
    void handleDisassemble(int requestSeq, nlohmann::json const& args);
    void handleSetVariable(int requestSeq, nlohmann::json const& args);
    void handleSource(int requestSeq, nlohmann::json const& args);
    void handleLoadedSources(int requestSeq);
    void handleRestart(int requestSeq, nlohmann::json const& args);
    void handleCompletions(int requestSeq, nlohmann::json const& args);
    void handleSetInstructionBreakpoints(int requestSeq, nlohmann::json const& args);
    void handleCancel(int requestSeq);

    // Execution
    void executeProgram();

    // Protocol logging
    void logMessage(std::string_view direction, nlohmann::json const& message);

    // State
    std::istream& _input;
    std::ostream& _output;
    int _seq = 0;                      ///< Monotonic sequence counter for sent messages
    bool _initialized = false;         ///< True after successful initialize handshake
    bool _configurationDone = false;   ///< True after configurationDone request
    bool _terminated = false;          ///< True after program termination
    bool _disconnectRequested = false; ///< True when disconnect requested

    // Runtime & execution
    CoreVM::Runtime _runtime;
    CoreVM::diagnostics::BufferedReport _report;
    InitializeRequestArguments _initArgs;
    LaunchRequestArguments _launchArgs;
    bool _programReady = false; ///< True after successful compilation

    // Debug session (owns Program, Runner, BreakpointManager)
    std::unique_ptr<DebugSession> _session;

    // Protocol logging
    std::unique_ptr<std::ofstream> _logFile;

    /// Buffer for capturing output during REPL evaluation (nullptr when not capturing).
    std::string* _outputCaptureBuffer = nullptr;

    /// Evaluates a full expression via compilation and execution.
    /// @param expression The expression to evaluate
    /// @param frameId Stack frame context for variable bindings
    /// @return Evaluation result, or nullopt on compilation/runtime failure
    std::optional<EvaluateResult> evaluateReplExpression(std::string const& expression, int frameId);
};

} // namespace endo::dap
