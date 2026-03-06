// SPDX-License-Identifier: Apache-2.0
#include "DapServer.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/builtins/TypeFormatters.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <editor-protocol/JsonTransport.hpp>

#include <CoreVM/TargetCodeGenerator.hpp>
#include <CoreVM/vm/Runner.hpp>

#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "DebugSession.hpp"

namespace endo::dap
{

using namespace endo::editor_protocol;

DapServer::DapServer(std::istream& input, std::ostream& output): _input(input), _output(output)
{
    // Register runtime with real callbacks for script execution.
    // print/println capture output and send as DAP output events.
    auto resolver = [this](std::string_view name,
                           size_t arity) -> std::optional<CoreVM::NativeCallback::Functor> {
        using Functor = CoreVM::NativeCallback::Functor;

        if (name == "print" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto const& text = args.getString(1);
                sendEvent("output", { { "category", "stdout" }, { "output", text } });
            });

        if (name == "println" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto const& text = args.getString(1);
                sendEvent("output", { { "category", "stdout" }, { "output", text + "\n" } });
            });

        if (name == "display_result" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto const rawVal = static_cast<uint64_t>(args.getInt(1));
                auto const text = builtins::valueToString(rawVal, args.caller());
                sendEvent("output", { { "category", "stdout" }, { "output", text + "\n" } });
            });

        // Fall back to shared stateless implementations for everything else.
        return builtins::resolveSharedImpl(name, arity);
    };

    registerFSharpBuiltins(_runtime, resolver);
    registerShellBuiltins(_runtime, resolver);
    registerInternalBuiltins(_runtime, resolver);
    registerStructuredBuiltins(_runtime, resolver);
}

DapServer::DapServer(): DapServer(std::cin, std::cout)
{
}

DapServer::~DapServer() = default;

void DapServer::setLogFile(std::string const& path)
{
    _logFile = std::make_unique<std::ofstream>(path, std::ios::app);
}

void DapServer::logMessage(std::string_view direction, nlohmann::json const& message)
{
    if (!_logFile)
        return;

    auto const now = std::chrono::system_clock::now();
    auto const time = std::chrono::system_clock::to_time_t(now);
    auto const ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    *_logFile << std::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] {} {}\n",
                             tm.tm_year + 1900,
                             tm.tm_mon + 1,
                             tm.tm_mday,
                             tm.tm_hour,
                             tm.tm_min,
                             tm.tm_sec,
                             ms,
                             direction,
                             message.dump());
    _logFile->flush();
}

int DapServer::run()
{
    while (!_disconnectRequested)
    {
        auto message = readMessage(_input);
        if (!message.has_value())
            break;
        logMessage("recv", *message);
        dispatch(*message);
    }
    return 0;
}

void DapServer::dispatch(nlohmann::json const& message)
{
    auto const type = message.value("type", std::string {});
    if (type != "request")
        return;

    auto const command = message.value("command", std::string {});
    auto const requestSeq = message.value("seq", 0);
    auto const args = message.value("arguments", nlohmann::json::object());

    if (command == "initialize")
    {
        auto const body = handleInitialize(args);
        sendResponse(requestSeq, command, body);
        sendEvent("initialized");
    }
    else if (!_initialized)
    {
        sendErrorResponse(requestSeq, command, "Server not yet initialized");
    }
    else if (command == "configurationDone")
    {
        handleConfigurationDone(requestSeq);
    }
    else if (command == "launch")
    {
        handleLaunch(requestSeq, args);
    }
    else if (command == "disconnect")
    {
        handleDisconnect(requestSeq, args);
    }
    else if (command == "terminate")
    {
        handleTerminate(requestSeq);
    }
    else if (command == "setBreakpoints")
    {
        handleSetBreakpoints(requestSeq, args);
    }
    else if (command == "setFunctionBreakpoints")
    {
        handleSetFunctionBreakpoints(requestSeq, args);
    }
    else if (command == "breakpointLocations")
    {
        handleBreakpointLocations(requestSeq, args);
    }
    else if (command == "continue")
    {
        handleContinue(requestSeq, args);
    }
    else if (command == "next")
    {
        handleNext(requestSeq, args);
    }
    else if (command == "stepIn")
    {
        handleStepIn(requestSeq, args);
    }
    else if (command == "stepOut")
    {
        handleStepOut(requestSeq, args);
    }
    else if (command == "pause")
    {
        handlePause(requestSeq, args);
    }
    else if (command == "threads")
    {
        handleThreads(requestSeq);
    }
    else if (command == "stackTrace")
    {
        handleStackTrace(requestSeq, args);
    }
    else if (command == "scopes")
    {
        handleScopes(requestSeq, args);
    }
    else if (command == "variables")
    {
        handleVariables(requestSeq, args);
    }
    else if (command == "evaluate")
    {
        handleEvaluate(requestSeq, args);
    }
    else if (command == "setExceptionBreakpoints")
    {
        handleSetExceptionBreakpoints(requestSeq, args);
    }
    else if (command == "exceptionInfo")
    {
        handleExceptionInfo(requestSeq, args);
    }
    else if (command == "disassemble")
    {
        handleDisassemble(requestSeq, args);
    }
    else if (command == "setVariable")
    {
        handleSetVariable(requestSeq, args);
    }
    else if (command == "source")
    {
        handleSource(requestSeq, args);
    }
    else if (command == "loadedSources")
    {
        handleLoadedSources(requestSeq);
    }
    else if (command == "restart")
    {
        handleRestart(requestSeq, args);
    }
    else if (command == "completions")
    {
        handleCompletions(requestSeq, args);
    }
    else if (command == "setInstructionBreakpoints")
    {
        handleSetInstructionBreakpoints(requestSeq, args);
    }
    else if (command == "cancel")
    {
        handleCancel(requestSeq);
    }
    else
    {
        sendErrorResponse(requestSeq, command, "Unsupported command: " + command);
    }
}

nlohmann::json DapServer::handleInitialize(nlohmann::json const& args)
{
    _initialized = true;

    // Store client capabilities for linesStartAt1/columnsStartAt1
    _initArgs = args.get<InitializeRequestArguments>();

    Capabilities caps;
    caps.supportsConfigurationDoneRequest = true;
    caps.supportsFunctionBreakpoints = true;
    caps.supportsBreakpointLocationsRequest = true;
    caps.supportsEvaluateForHovers = true;
    caps.supportsVariableType = true;
    caps.supportsConditionalBreakpoints = true;
    caps.supportsHitConditionalBreakpoints = true;
    caps.supportsLogPoints = true;
    caps.supportsSteppingGranularity = true;
    caps.supportsDisassembleRequest = true;
    caps.supportsSetVariable = true;
    caps.supportsExceptionInfoRequest = true;
    caps.supportsLoadedSourcesRequest = true;
    caps.supportsTerminateRequest = true;
    caps.supportsRestartRequest = true;
    caps.supportsCompletionsRequest = true;
    caps.supportsInstructionBreakpoints = true;
    caps.exceptionBreakpointFilters = {
        { "runtime-error", "Runtime Errors", "Break on runtime errors", false },
        { "all", "All Errors", "Break on all errors", false },
    };

    nlohmann::json body;
    to_json(body, caps);
    return body;
}

void DapServer::handleConfigurationDone(int requestSeq)
{
    _configurationDone = true;
    sendResponse(requestSeq, "configurationDone");

    if (_programReady)
        executeProgram();
}

void DapServer::handleLaunch(int requestSeq, nlohmann::json const& args)
{
    _launchArgs = args.get<LaunchRequestArguments>();

    // Read the script file
    std::ifstream file(_launchArgs.program);
    if (!file)
    {
        auto const errorMsg = "Cannot open file: " + _launchArgs.program;
        sendEvent("output", { { "category", "stderr" }, { "output", errorMsg + "\n" } });
        sendErrorResponse(requestSeq, "launch", errorMsg);
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    auto source = ss.str();

    // Strip shebang line if present
    if (source.starts_with("#!"))
    {
        auto const pos = source.find('\n');
        if (pos != std::string::npos)
            source = source.substr(pos + 1);
        else
            source.clear();
    }

    // Parse (pass filename so location tables carry the source path)
    Parser parser(_runtime, _report, std::make_unique<StringSource>(source, _launchArgs.program));
    auto ast = parser.parse();
    if (!ast || _report.containsFailures())
    {
        std::string errorMsg = "Compilation failed:";
        for (auto const& msg: _report)
            errorMsg += "\n  " + msg.text;
        sendEvent("output", { { "category", "stderr" }, { "output", errorMsg + "\n" } });
        sendErrorResponse(requestSeq, "launch", errorMsg);
        _report.clear();
        return;
    }

    // Generate IR
    auto ir = IRGenerator::generate(*ast, _report, _runtime);
    if (!ir || _report.containsFailures())
    {
        std::string errorMsg = "IR generation failed:";
        for (auto const& msg: _report)
            errorMsg += "\n  " + msg.text;
        sendEvent("output", { { "category", "stderr" }, { "output", errorMsg + "\n" } });
        sendErrorResponse(requestSeq, "launch", errorMsg);
        _report.clear();
        return;
    }

    // Generate bytecode
    CoreVM::TargetCodeGenerator codegen;
    auto program = codegen.generate(ir.get());
    if (!program)
    {
        sendEvent("output", { { "category", "stderr" }, { "output", "Code generation failed\n" } });
        sendErrorResponse(requestSeq, "launch", "Code generation failed");
        return;
    }

    // Register type formatters
    builtins::registerBuiltinFormatters(program->constants().typeRegistry());

    // Link program to runtime
    if (!program->link(&_runtime, &_report))
    {
        std::string errorMsg = "Link failed:";
        for (auto const& msg: _report)
            errorMsg += "\n  " + msg.text;
        sendEvent("output", { { "category", "stderr" }, { "output", errorMsg + "\n" } });
        sendErrorResponse(requestSeq, "launch", errorMsg);
        _report.clear();
        return;
    }

    // Create debug session (owns the program)
    auto eventSender = [this](std::string const& event, nlohmann::json body) {
        sendEvent(event, std::move(body));
    };
    _session = std::make_unique<DebugSession>(std::move(program), _launchArgs, std::move(eventSender));

    _programReady = true;
    sendResponse(requestSeq, "launch");
    sendEvent("process", { { "name", _launchArgs.program }, { "startMethod", "launch" } });

    // If configurationDone already received, execute immediately
    if (_configurationDone)
        executeProgram();
}

void DapServer::executeProgram()
{
    if (_terminated || !_session)
        return;

    sendEvent("thread", { { "reason", "started" }, { "threadId", 1 } });

    auto const completed = _session->startExecution();
    if (completed)
    {
        _terminated = true;
        sendEvent("thread", { { "reason", "exited" }, { "threadId", 1 } });
    }
    // If not completed, VM is suspended (breakpoint hit).
    // The message loop continues processing requests.
}

void DapServer::handleSetBreakpoints(int requestSeq, nlohmann::json const& args)
{
    auto const sourcePath = args.contains("source") && args.at("source").contains("path")
                                ? args.at("source").at("path").get<std::string>()
                                : std::string {};

    std::vector<SourceBreakpoint> breakpoints;
    if (args.contains("breakpoints"))
        breakpoints = args.at("breakpoints").get<std::vector<SourceBreakpoint>>();

    CoreVM::Program const* program = _session ? &_session->program() : nullptr;
    auto result = _session
                      ? _session->breakpointManager().setSourceBreakpoints(sourcePath, breakpoints, program)
                      : std::vector<Breakpoint> {};

    // If no session, return unverified breakpoints
    if (!_session)
    {
        for (auto const& bp: breakpoints)
        {
            Breakpoint unverified;
            unverified.id = 0;
            unverified.verified = false;
            unverified.line = bp.line;
            unverified.sourcePath = sourcePath;
            result.push_back(std::move(unverified));
        }
    }

    nlohmann::json responseBreakpoints = nlohmann::json::array();
    for (auto const& bp: result)
    {
        nlohmann::json bpJson;
        to_json(bpJson, bp);
        responseBreakpoints.push_back(std::move(bpJson));
    }

    sendResponse(requestSeq, "setBreakpoints", { { "breakpoints", responseBreakpoints } });
}

void DapServer::handleSetFunctionBreakpoints(int requestSeq, nlohmann::json const& args)
{
    std::vector<FunctionBreakpoint> breakpoints;
    if (args.contains("breakpoints"))
        breakpoints = args.at("breakpoints").get<std::vector<FunctionBreakpoint>>();

    CoreVM::Program const* program = _session ? &_session->program() : nullptr;
    auto result = _session ? _session->breakpointManager().setFunctionBreakpoints(breakpoints, program)
                           : std::vector<Breakpoint> {};

    nlohmann::json responseBreakpoints = nlohmann::json::array();
    for (auto const& bp: result)
    {
        nlohmann::json bpJson;
        to_json(bpJson, bp);
        responseBreakpoints.push_back(std::move(bpJson));
    }

    sendResponse(requestSeq, "setFunctionBreakpoints", { { "breakpoints", responseBreakpoints } });
}

void DapServer::handleBreakpointLocations(int requestSeq, nlohmann::json const& args)
{
    auto const sourcePath = args.contains("source") && args.at("source").contains("path")
                                ? args.at("source").at("path").get<std::string>()
                                : std::string {};
    auto const startLine = args.value("line", 1);
    auto const endLine = args.value("endLine", startLine);

    std::vector<BreakpointLocation> locations;
    if (_session)
        locations =
            BreakpointManager::breakpointLocations(sourcePath, startLine, endLine, _session->program());

    nlohmann::json responseLocations = nlohmann::json::array();
    for (auto const& loc: locations)
    {
        nlohmann::json locJson;
        to_json(locJson, loc);
        responseLocations.push_back(std::move(locJson));
    }

    sendResponse(requestSeq, "breakpointLocations", { { "breakpoints", responseLocations } });
}

void DapServer::handleContinue(int requestSeq, nlohmann::json const& /*args*/)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "continue", "Not stopped");
        return;
    }

    sendResponse(requestSeq, "continue", { { "allThreadsContinued", true } });
    sendEvent("continued", { { "threadId", 1 }, { "allThreadsContinued", true } });

    auto const completed = _session->continueExecution();
    if (completed)
    {
        _terminated = true;
        sendEvent("thread", { { "reason", "exited" }, { "threadId", 1 } });
    }
}

void DapServer::handleNext(int requestSeq, nlohmann::json const& args)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "next", "Not stopped");
        return;
    }

    auto const granularity = args.value("granularity", std::string { "line" }) == "instruction"
                                 ? SteppingGranularity::Instruction
                                 : SteppingGranularity::Line;
    _session->setStepMode(StepMode::StepOver, granularity);
    sendResponse(requestSeq, "next");
    sendEvent("continued", { { "threadId", 1 }, { "allThreadsContinued", true } });

    auto const completed = _session->resumeExecution();
    if (completed)
    {
        _terminated = true;
        sendEvent("thread", { { "reason", "exited" }, { "threadId", 1 } });
    }
}

void DapServer::handleStepIn(int requestSeq, nlohmann::json const& args)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "stepIn", "Not stopped");
        return;
    }

    auto const granularity = args.value("granularity", std::string { "line" }) == "instruction"
                                 ? SteppingGranularity::Instruction
                                 : SteppingGranularity::Line;
    _session->setStepMode(StepMode::StepIn, granularity);
    sendResponse(requestSeq, "stepIn");
    sendEvent("continued", { { "threadId", 1 }, { "allThreadsContinued", true } });

    auto const completed = _session->resumeExecution();
    if (completed)
    {
        _terminated = true;
        sendEvent("thread", { { "reason", "exited" }, { "threadId", 1 } });
    }
}

void DapServer::handleStepOut(int requestSeq, nlohmann::json const& args)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "stepOut", "Not stopped");
        return;
    }

    auto const granularity = args.value("granularity", std::string { "line" }) == "instruction"
                                 ? SteppingGranularity::Instruction
                                 : SteppingGranularity::Line;
    _session->setStepMode(StepMode::StepOut, granularity);
    sendResponse(requestSeq, "stepOut");
    sendEvent("continued", { { "threadId", 1 }, { "allThreadsContinued", true } });

    auto const completed = _session->resumeExecution();
    if (completed)
    {
        _terminated = true;
        sendEvent("thread", { { "reason", "exited" }, { "threadId", 1 } });
    }
}

void DapServer::handlePause(int requestSeq, nlohmann::json const& /*args*/)
{
    if (!_session)
    {
        sendErrorResponse(requestSeq, "pause", "No active session");
        return;
    }

    _session->requestPause();
    sendResponse(requestSeq, "pause");
}

void DapServer::handleThreads(int requestSeq)
{
    nlohmann::json threads = nlohmann::json::array();
    threads.push_back({ { "id", 1 }, { "name", "main" } });
    sendResponse(requestSeq, "threads", { { "threads", threads } });
}

void DapServer::handleStackTrace(int requestSeq, nlohmann::json const& args)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "stackTrace", "Not stopped");
        return;
    }

    auto const startFrame = args.value("startFrame", 0);
    auto const levels = args.value("levels", 0);
    auto const frames = _session->getStackTrace(startFrame, levels);

    nlohmann::json stackFrames = nlohmann::json::array();
    for (auto const& f: frames)
    {
        nlohmann::json fj;
        to_json(fj, f);
        stackFrames.push_back(std::move(fj));
    }

    sendResponse(requestSeq,
                 "stackTrace",
                 { { "stackFrames", stackFrames }, { "totalFrames", static_cast<int>(frames.size()) } });
}

void DapServer::handleScopes(int requestSeq, nlohmann::json const& args)
{
    auto const frameId = args.value("frameId", 0);
    auto const scopes = _session ? _session->getScopes(frameId) : std::vector<Scope> {};

    nlohmann::json scopeArray = nlohmann::json::array();
    for (auto const& s: scopes)
    {
        nlohmann::json sj;
        to_json(sj, s);
        scopeArray.push_back(std::move(sj));
    }

    sendResponse(requestSeq, "scopes", { { "scopes", scopeArray } });
}

void DapServer::handleVariables(int requestSeq, nlohmann::json const& args)
{
    auto const variablesReference = args.value("variablesReference", 0);
    auto const variables = _session ? _session->getVariables(variablesReference) : std::vector<Variable> {};

    nlohmann::json varArray = nlohmann::json::array();
    for (auto const& v: variables)
    {
        nlohmann::json vj;
        to_json(vj, v);
        varArray.push_back(std::move(vj));
    }

    sendResponse(requestSeq, "variables", { { "variables", varArray } });
}

void DapServer::handleEvaluate(int requestSeq, nlohmann::json const& args)
{
    auto const expression = args.value("expression", std::string {});
    auto const frameId = args.value("frameId", 0);
    auto const context = args.value("context", std::string { "hover" });

    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "evaluate", "Not stopped");
        return;
    }

    // For REPL context, try full expression evaluation first
    if (context == "repl")
    {
        auto replResult = evaluateReplExpression(expression, frameId);
        if (replResult)
        {
            nlohmann::json body;
            to_json(body, *replResult);
            sendResponse(requestSeq, "evaluate", std::move(body));
            return;
        }
        // Fall through to ConditionEvaluator on failure
    }

    auto const result = _session->evaluate(expression, frameId, context);
    if (!result)
    {
        sendErrorResponse(requestSeq, "evaluate", "Could not evaluate expression: " + expression);
        return;
    }

    nlohmann::json body;
    to_json(body, *result);
    sendResponse(requestSeq, "evaluate", std::move(body));
}

void DapServer::handleSetExceptionBreakpoints(int requestSeq, nlohmann::json const& args)
{
    std::vector<std::string> filters;
    if (args.contains("filters"))
        filters = args.at("filters").get<std::vector<std::string>>();

    if (_session)
        _session->setExceptionFilters(filters);

    sendResponse(requestSeq, "setExceptionBreakpoints");
}

void DapServer::handleExceptionInfo(int requestSeq, nlohmann::json const& /*args*/)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "exceptionInfo", "Not stopped");
        return;
    }

    auto const info = _session->getExceptionInfo();
    if (!info.has_value())
    {
        sendErrorResponse(requestSeq, "exceptionInfo", "No exception");
        return;
    }

    nlohmann::json body = {
        { "exceptionId", "runtime-error" },
        { "description", info->message },
        { "breakMode", "always" },
    };

    if (!info->location.filename.empty())
    {
        body["details"] = nlohmann::json {
            { "message", info->format() },
        };
    }

    sendResponse(requestSeq, "exceptionInfo", std::move(body));
}

void DapServer::handleDisassemble(int requestSeq, nlohmann::json const& args)
{
    if (!_session)
    {
        sendErrorResponse(requestSeq, "disassemble", "No active session");
        return;
    }

    auto const memoryReference = args.value("memoryReference", std::string {});
    auto const instructionCount = args.value("instructionCount", 100);
    auto const instructionOffset = args.value("instructionOffset", 0);

    auto instructions = _session->disassemble(memoryReference, instructionCount, instructionOffset);
    sendResponse(requestSeq, "disassemble", { { "instructions", std::move(instructions) } });
}

void DapServer::handleSetVariable(int requestSeq, nlohmann::json const& args)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "setVariable", "Not stopped");
        return;
    }

    auto const variablesReference = args.value("variablesReference", 0);
    auto const name = args.value("name", std::string {});
    auto const value = args.value("value", std::string {});

    auto result = _session->setVariable(variablesReference, name, value);
    if (!result)
    {
        sendErrorResponse(requestSeq, "setVariable", "Could not set variable: " + name);
        return;
    }

    sendResponse(requestSeq,
                 "setVariable",
                 { { "value", result->value }, { "type", result->type }, { "variablesReference", 0 } });
}

void DapServer::handleSource(int requestSeq, nlohmann::json const& args)
{
    auto const sourcePath = args.contains("source") && args.at("source").contains("path")
                                ? args.at("source").at("path").get<std::string>()
                                : std::string {};

    if (sourcePath.empty() || !_session)
    {
        sendErrorResponse(requestSeq, "source", "Source not available");
        return;
    }

    auto const content = _session->getSourceContent(sourcePath);
    if (!content)
    {
        sendErrorResponse(requestSeq, "source", "Could not read source: " + sourcePath);
        return;
    }

    sendResponse(requestSeq, "source", { { "content", *content } });
}

void DapServer::handleLoadedSources(int requestSeq)
{
    nlohmann::json sources = nlohmann::json::array();
    if (_session)
    {
        for (auto const& path: _session->loadedSources())
        {
            nlohmann::json src = { { "path", path } };
            auto const pos = path.find_last_of('/');
            src["name"] = (pos != std::string::npos) ? path.substr(pos + 1) : path;
            sources.push_back(std::move(src));
        }
    }
    sendResponse(requestSeq, "loadedSources", { { "sources", sources } });
}

void DapServer::handleDisconnect(int requestSeq, nlohmann::json const& args)
{
    // Honor terminateDebuggee (defaults to true for launch sessions)
    auto const terminateDebuggee = args.value("terminateDebuggee", true);
    if (terminateDebuggee && _session && !_terminated)
    {
        _terminated = true;
        sendEvent("thread", { { "reason", "exited" }, { "threadId", 1 } });
        sendEvent("terminated", nlohmann::json::object());
    }

    _disconnectRequested = true;
    sendResponse(requestSeq, "disconnect");
}

void DapServer::handleTerminate(int requestSeq)
{
    _terminated = true;
    sendEvent("terminated");
    sendResponse(requestSeq, "terminate");
}

void DapServer::sendResponse(int requestSeq, std::string const& command, nlohmann::json body)
{
    nlohmann::json response = {
        { "seq", ++_seq },   { "type", "response" }, { "request_seq", requestSeq },
        { "success", true }, { "command", command }, { "body", std::move(body) },
    };
    logMessage("send", response);
    writeMessage(_output, response);
}

void DapServer::sendErrorResponse(int requestSeq, std::string const& command, std::string const& message)
{
    nlohmann::json response = {
        { "seq", ++_seq },
        { "type", "response" },
        { "request_seq", requestSeq },
        { "success", false },
        { "command", command },
        { "message", message },
        { "body", { { "error", { { "id", requestSeq }, { "format", message } } } } },
    };
    logMessage("send", response);
    writeMessage(_output, response);
}

void DapServer::sendEvent(std::string const& event, nlohmann::json body)
{
    // Intercept stdout output when capturing for REPL evaluation
    if (_outputCaptureBuffer && event == "output" && body.value("category", "") == "stdout")
    {
        *_outputCaptureBuffer += body.value("output", "");
        return;
    }

    nlohmann::json msg = {
        { "seq", ++_seq },
        { "type", "event" },
        { "event", event },
        { "body", std::move(body) },
    };
    logMessage("send", msg);
    writeMessage(_output, msg);
}

void DapServer::handleRestart(int requestSeq, nlohmann::json const& /*args*/)
{
    if (!_session)
    {
        sendErrorResponse(requestSeq, "restart", "No active session to restart");
        return;
    }

    // Destroy the current session
    _session.reset();
    _terminated = false;
    _programReady = false;

    // Re-read and recompile the script
    std::ifstream file(_launchArgs.program);
    if (!file)
    {
        sendErrorResponse(requestSeq, "restart", "Cannot open file: " + _launchArgs.program);
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    auto source = ss.str();

    if (source.starts_with("#!"))
    {
        auto const pos = source.find('\n');
        if (pos != std::string::npos)
            source = source.substr(pos + 1);
        else
            source.clear();
    }

    Parser parser(_runtime, _report, std::make_unique<StringSource>(source, _launchArgs.program));
    auto ast = parser.parse();
    if (!ast || _report.containsFailures())
    {
        std::string errorMsg = "Restart compilation failed:";
        for (auto const& msg: _report)
            errorMsg += "\n  " + msg.text;
        sendErrorResponse(requestSeq, "restart", errorMsg);
        _report.clear();
        return;
    }

    auto ir = IRGenerator::generate(*ast, _report, _runtime);
    if (!ir || _report.containsFailures())
    {
        std::string errorMsg = "Restart IR generation failed:";
        for (auto const& msg: _report)
            errorMsg += "\n  " + msg.text;
        sendErrorResponse(requestSeq, "restart", errorMsg);
        _report.clear();
        return;
    }

    CoreVM::TargetCodeGenerator codegen;
    auto program = codegen.generate(ir.get());
    if (!program)
    {
        sendErrorResponse(requestSeq, "restart", "Code generation failed");
        return;
    }

    builtins::registerBuiltinFormatters(program->constants().typeRegistry());

    if (!program->link(&_runtime, &_report))
    {
        std::string errorMsg = "Restart link failed:";
        for (auto const& msg: _report)
            errorMsg += "\n  " + msg.text;
        sendErrorResponse(requestSeq, "restart", errorMsg);
        _report.clear();
        return;
    }

    auto eventSender = [this](std::string const& event, nlohmann::json body) {
        sendEvent(event, std::move(body));
    };
    _session = std::make_unique<DebugSession>(std::move(program), _launchArgs, std::move(eventSender));

    _programReady = true;
    sendResponse(requestSeq, "restart");
    sendEvent("process", { { "name", _launchArgs.program }, { "startMethod", "launch" } });

    executeProgram();
}

void DapServer::handleCompletions(int requestSeq, nlohmann::json const& args)
{
    if (!_session || !_session->isStopped())
    {
        sendErrorResponse(requestSeq, "completions", "Not stopped");
        return;
    }

    auto const text = args.value("text", std::string {});
    auto const frameId = args.value("frameId", 0);

    auto const completions = _session->getCompletions(text, frameId);

    nlohmann::json targets = nlohmann::json::array();
    for (auto const& item: completions)
    {
        nlohmann::json cj;
        to_json(cj, item);
        targets.push_back(std::move(cj));
    }

    sendResponse(requestSeq, "completions", { { "targets", targets } });
}

void DapServer::handleSetInstructionBreakpoints(int requestSeq, nlohmann::json const& args)
{
    std::vector<InstructionBreakpoint> breakpoints;
    if (args.contains("breakpoints"))
        breakpoints = args.at("breakpoints").get<std::vector<InstructionBreakpoint>>();

    auto result = _session ? _session->breakpointManager().setInstructionBreakpoints(breakpoints)
                           : std::vector<Breakpoint> {};

    nlohmann::json responseBreakpoints = nlohmann::json::array();
    for (auto const& bp: result)
    {
        nlohmann::json bpJson;
        to_json(bpJson, bp);
        responseBreakpoints.push_back(std::move(bpJson));
    }

    sendResponse(requestSeq, "setInstructionBreakpoints", { { "breakpoints", responseBreakpoints } });
}

void DapServer::handleCancel(int requestSeq)
{
    // No-op for synchronous server — all requests complete before returning
    sendResponse(requestSeq, "cancel");
}

std::optional<EvaluateResult> DapServer::evaluateReplExpression(std::string const& expression, int frameId)
{
    if (!_session || !_session->isStopped())
        return std::nullopt;

    // Build variable preamble from current frame's locals
    auto const localsRef = ((frameId + 1) * 1000) + 1;
    auto const localVars = _session->getVariables(localsRef);

    std::string preamble;
    for (auto const& var: localVars)
    {
        // Only inject primitive types as let bindings
        if (var.type == "number" || var.type == "boolean" || var.type == "float" || var.type == "string")
            preamble += "let " + var.name + " = " + var.value + "\n";
    }

    // Also inject globals
    auto const globalsRef = ((frameId + 1) * 1000) + 2;
    auto const globalVars = _session->getVariables(globalsRef);
    for (auto const& var: globalVars)
    {
        if (var.type == "number" || var.type == "boolean" || var.type == "float" || var.type == "string")
            preamble += "let " + var.name + " = " + var.value + "\n";
    }

    auto const fullSource = preamble + "println (" + expression + ")";

    // Use a local report to avoid interfering with the session's report
    CoreVM::diagnostics::BufferedReport localReport;

    // Parse
    Parser parser(_runtime, localReport, std::make_unique<StringSource>(fullSource, "<repl-eval>"));
    auto ast = parser.parse();
    if (!ast || localReport.containsFailures())
        return std::nullopt;

    // Generate IR
    auto ir = IRGenerator::generate(*ast, localReport, _runtime);
    if (!ir || localReport.containsFailures())
        return std::nullopt;

    // Generate bytecode
    CoreVM::TargetCodeGenerator codegen;
    auto program = codegen.generate(ir.get());
    if (!program)
        return std::nullopt;

    // Register type formatters
    builtins::registerBuiltinFormatters(program->constants().typeRegistry());

    // Link
    if (!program->link(&_runtime, &localReport))
        return std::nullopt;

    // Find main function
    auto const* fn = program->findFunction("@main");
    if (!fn)
        return std::nullopt;

    // Execute with output capture
    std::string capturedOutput;
    _outputCaptureBuffer = &capturedOutput;

    // RAII guard to ensure capture buffer is reset
    struct CaptureGuard
    {
        std::string** buf;

        ~CaptureGuard() { *buf = nullptr; }
    } guard { &_outputCaptureBuffer };

    CoreVM::Runner::Globals evalGlobals;
    auto runner = std::make_unique<CoreVM::Runner>(
        fn, nullptr, &evalGlobals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
    auto result = runner->runWithResult();

    if (!result.has_value())
        return std::nullopt; // Runtime error

    // Trim trailing newline from println
    if (!capturedOutput.empty() && capturedOutput.back() == '\n')
        capturedOutput.pop_back();

    EvaluateResult evalResult;
    evalResult.result = capturedOutput;
    evalResult.variablesReference = 0;

    // Try to infer type
    if (capturedOutput == "true" || capturedOutput == "false")
        evalResult.type = "boolean";
    else
    {
        // Try to parse as number
        CoreVM::CoreNumber numVal = 0;
        auto [ptr, ec] =
            std::from_chars(capturedOutput.data(), capturedOutput.data() + capturedOutput.size(), numVal);
        if (ec == std::errc() && ptr == capturedOutput.data() + capturedOutput.size())
            evalResult.type = "number";
        else
            evalResult.type = "string";
    }

    return evalResult;
}

} // namespace endo::dap
