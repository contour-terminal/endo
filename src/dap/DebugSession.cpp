// SPDX-License-Identifier: Apache-2.0
#include "DebugSession.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <CoreVM/CoreTypes.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>
#include <CoreVM/vm/Program.hpp>
#include <CoreVM/vm/Runner.hpp>

#include <algorithm>
#include <bit>
#include <charconv>
#include <format>
#include <fstream>
#include <ranges>
#include <set>
#include <sstream>
#include <utility>

#include "ConditionEvaluator.hpp"

namespace endo::dap
{

namespace
{

    /// Converts a StopReason to its DAP string representation.
    constexpr char const* stopReasonToString(StopReason reason) noexcept
    {
        switch (reason)
        {
            case StopReason::Breakpoint: return "breakpoint";
            case StopReason::Entry: return "entry";
            case StopReason::Step: return "step";
            case StopReason::Pause: return "pause";
            case StopReason::Exception: return "exception";
            case StopReason::None: return "unknown";
        }
        return "unknown";
    }

    /// Encodes a function index and instruction pointer as a hex address string.
    /// Format: "0x{funcIndex:8}{ip:8}" (16 hex digits total).
    std::string encodeAddress(size_t funcIndex, size_t ip)
    {
        auto const address =
            (static_cast<uint64_t>(funcIndex) << 32) | static_cast<uint64_t>(ip & 0xFFFFFFFF);
        return std::format("0x{:016X}", address);
    }

    /// Decodes a hex address string into (functionIndex, instructionPointer).
    std::pair<size_t, size_t> decodeAddress(std::string const& address)
    {
        uint64_t val = 0;
        // Skip "0x" prefix if present
        auto const* start = address.data();
        auto const* end = address.data() + address.size();
        if (address.starts_with("0x") || address.starts_with("0X"))
            start += 2;
        std::from_chars(start, end, val, 16);
        return { static_cast<size_t>(val >> 32), static_cast<size_t>(val & 0xFFFFFFFF) };
    }

} // namespace

DebugSession::DebugSession(std::unique_ptr<CoreVM::Program> program,
                           LaunchRequestArguments launchArgs,
                           EventSender eventSender):
    _program(std::move(program)), _launchArgs(std::move(launchArgs)), _eventSender(std::move(eventSender))
{
    // Collect unique source filenames from all function location tables
    std::set<std::string> sources;
    auto const& functions = _program->constants().getFunctions();
    for (size_t i = 0; i < functions.size(); ++i)
    {
        for (auto const& [ip, loc]: _program->constants().getFunctionLocationTable(i))
        {
            if (!loc.filename.empty())
                sources.insert(loc.filename);
        }
    }
    _loadedSources.assign(sources.begin(), sources.end());
}

void DebugSession::setStepMode(StepMode mode, SteppingGranularity granularity)
{
    _stepMode = mode;
    _steppingGranularity = granularity;
    _lastStopFile.clear();
    _lastStopLine = -1;

    if (_runner)
    {
        _stepStartDepth = _runner->callStackDepth();
        auto const& loc = _runner->function()->locationOf(_runner->getInstructionPointer());
        _stepStartLine = static_cast<int>(loc.begin.line);
        _stepStartFile = loc.filename;
    }
}

void DebugSession::requestPause()
{
    _pauseRequested = true;
}

bool DebugSession::startExecution()
{
    auto const* fn = _program->findFunction("@main");
    if (!fn)
    {
        _eventSender("output", { { "category", "stderr" }, { "output", "No @main function found\n" } });
        _terminated = true;
        return true;
    }

    CoreVM::Runner::TraceLogger traceLogger;
    if (!_launchArgs.noDebug)
    {
        traceLogger = [this](CoreVM::Instruction instr, size_t ip, size_t sp) {
            onTrace(instr, ip, sp);
        };

        // Enable stopOnEntry if requested
        if (_launchArgs.stopOnEntry)
            _stopOnEntry = true;
    }

    _runner = std::make_unique<CoreVM::Runner>(
        fn, nullptr, &_globals, CoreVM::RuntimeConfig::defaultConfig(), std::move(traceLogger));

    // Set up error callback for exception breakpoints
    if (!_exceptionFilters.empty())
    {
        _runner->setErrorCallback([this](CoreVM::RuntimeError const& error) -> bool {
            if (_exceptionFilters.contains("all") || _exceptionFilters.contains("runtime-error"))
            {
                _currentException = error;
                return true; // suspend
            }
            return false;
        });
    }

    auto result = _runner->runWithResult();
    return handleRunResultWithError(std::move(result));
}

bool DebugSession::continueExecution()
{
    if (!_runner)
        return true;

    // Clear stop/step state but preserve last stop location
    // to avoid re-triggering on the same instruction after resume
    _stopReason = StopReason::None;
    resetVarRefMap();
    _hitBreakpointIds.clear();
    _stepMode = StepMode::None;
    _currentException.reset();

    auto result = _runner->resumeWithResult();
    return handleRunResultWithError(std::move(result));
}

bool DebugSession::resumeExecution()
{
    if (!_runner)
        return true;

    // Clear stop state but preserve step mode
    _stopReason = StopReason::None;
    resetVarRefMap();
    _hitBreakpointIds.clear();
    _currentException.reset();

    auto result = _runner->resumeWithResult();
    return handleRunResultWithError(std::move(result));
}

bool DebugSession::isStopped() const noexcept
{
    return _runner && _runner->state() == CoreVM::Runner::State::Suspended;
}

bool DebugSession::isTerminated() const noexcept
{
    return _terminated;
}

int DebugSession::allocateVarRef(uint64_t rawValue, CoreVM::TypedObject* obj) const
{
    auto const ref = _nextVarRef++;
    _varRefMap[ref] = { rawValue, obj };
    return ref;
}

void DebugSession::resetVarRefMap() const
{
    _varRefMap.clear();
    _nextVarRef = 100000;
}

bool DebugSession::isExpandable(CoreVM::TypedObject const* obj)
{
    if (!obj || !obj->type)
        return false;

    auto const typeId = obj->type->id;

    // Not expandable: Seq, Lazy, Callable
    if (typeId == CoreVM::BuiltinTypeId::Seq || typeId == CoreVM::BuiltinTypeId::Lazy
        || typeId == CoreVM::BuiltinTypeId::Callable)
        return false;

    // Sum types: expandable if current variant has payload slots > 0
    if (obj->type->kind == CoreVM::TypeKind::Sum)
    {
        auto const* variant = obj->type->getVariant(obj->tag);
        return variant && variant->payloadSlots > 0;
    }

    // Product types: expandable if has fields or slots
    if (obj->type->kind == CoreVM::TypeKind::Product)
        return !obj->type->fields.empty() || obj->type->slotCount > 0;

    return false;
}

int DebugSession::maybeAllocateChildRef(uint64_t rawValue) const
{
    if (!_runner || !_runner->isKnownObject(rawValue))
        return 0;

    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawValue));
    if (!isExpandable(obj))
        return 0;

    return allocateVarRef(rawValue, obj);
}

namespace
{
    /// Helper to determine the type name string for a given LiteralType.
    std::string literalTypeName(CoreVM::LiteralType type)
    {
        switch (type)
        {
            case CoreVM::LiteralType::Number: return "number";
            case CoreVM::LiteralType::String: return "string";
            case CoreVM::LiteralType::Boolean: return "boolean";
            case CoreVM::LiteralType::Float: return "float";
            default: return "object";
        }
    }
} // namespace

std::vector<Variable> DebugSession::enumerateChildren(int varRef) const
{
    std::vector<Variable> children;
    auto it = _varRefMap.find(varRef);
    if (it == _varRefMap.end() || !_runner)
        return children;

    auto* obj = it->second.second;
    if (!obj || !obj->type)
        return children;

    auto const typeId = obj->type->id;

    // --- List ---
    if (typeId == CoreVM::BuiltinTypeId::List)
    {
        auto const elemType = static_cast<CoreVM::LiteralType>(obj->getSlot(2));
        auto const* cur = obj;
        int index = 0;
        while (cur && cur->tag == 1 && index < 100) // tag 1 = Cons
        {
            auto const headVal = cur->getSlot(0);
            Variable child;
            child.name = "[" + std::to_string(index) + "]";
            child.value = builtins::slotValueToString(headVal, elemType, _runner.get());
            child.variablesReference = maybeAllocateChildRef(headVal);
            if (elemType == CoreVM::LiteralType::Void || elemType == CoreVM::LiteralType::Object)
            {
                if (_runner->isKnownObject(headVal))
                {
                    auto* childObj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(headVal));
                    child.type = childObj->type->name;
                }
                else
                {
                    child.type = "object";
                }
            }
            else
            {
                child.type = literalTypeName(elemType);
            }
            children.push_back(std::move(child));

            auto const tailVal = cur->getSlot(1);
            if (!_runner->isKnownObject(tailVal))
                break;
            cur = reinterpret_cast<CoreVM::TypedObject const*>(static_cast<uintptr_t>(tailVal));
            ++index;
        }
        // Add length info
        Variable lenVar;
        lenVar.name = "length";
        lenVar.value = std::to_string(index);
        lenVar.type = "number";
        lenVar.variablesReference = 0;
        children.push_back(std::move(lenVar));
        return children;
    }

    // --- Tuple2 ---
    if (typeId == CoreVM::BuiltinTypeId::Tuple2)
    {
        auto const packed = obj->getSlot(2);
        for (uint8_t i = 0; i < 2; ++i)
        {
            auto const elemType = CoreVM::unpackTypeTag(packed, i);
            auto const val = obj->getSlot(i);
            Variable child;
            child.name = "[" + std::to_string(i) + "]";
            child.value = builtins::slotValueToString(val, elemType, _runner.get());
            child.variablesReference = maybeAllocateChildRef(val);
            child.type = literalTypeName(elemType);
            children.push_back(std::move(child));
        }
        return children;
    }

    // --- Tuple3 ---
    if (typeId == CoreVM::BuiltinTypeId::Tuple3)
    {
        auto const packed = obj->getSlot(3);
        for (uint8_t i = 0; i < 3; ++i)
        {
            auto const elemType = CoreVM::unpackTypeTag(packed, i);
            auto const val = obj->getSlot(i);
            Variable child;
            child.name = "[" + std::to_string(i) + "]";
            child.value = builtins::slotValueToString(val, elemType, _runner.get());
            child.variablesReference = maybeAllocateChildRef(val);
            child.type = literalTypeName(elemType);
            children.push_back(std::move(child));
        }
        return children;
    }

    // --- Option (tag 0 = None, tag 1 = Some) ---
    if (typeId == CoreVM::BuiltinTypeId::Option)
    {
        Variable variantVar;
        variantVar.name = "variant";
        variantVar.value = (obj->tag == 1) ? "Some" : "None";
        variantVar.type = "string";
        variantVar.variablesReference = 0;
        children.push_back(std::move(variantVar));

        if (obj->tag == 1) // Some
        {
            auto const innerType = static_cast<CoreVM::LiteralType>(obj->getSlot(1));
            auto const val = obj->getSlot(0);
            Variable valueVar;
            valueVar.name = "value";
            valueVar.value = builtins::slotValueToString(val, innerType, _runner.get());
            valueVar.variablesReference = maybeAllocateChildRef(val);
            valueVar.type = literalTypeName(innerType);
            children.push_back(std::move(valueVar));
        }
        return children;
    }

    // --- Result (tag 0 = Error, tag 1 = Ok) ---
    if (typeId == CoreVM::BuiltinTypeId::Result)
    {
        Variable variantVar;
        variantVar.name = "variant";
        variantVar.value = (obj->tag == 1) ? "Ok" : "Error";
        variantVar.type = "string";
        variantVar.variablesReference = 0;
        children.push_back(std::move(variantVar));

        auto const innerType = static_cast<CoreVM::LiteralType>(obj->getSlot(1));
        auto const val = obj->getSlot(0);
        Variable valueVar;
        valueVar.name = "value";
        valueVar.value = builtins::slotValueToString(val, innerType, _runner.get());
        valueVar.variablesReference = maybeAllocateChildRef(val);
        valueVar.type = literalTypeName(innerType);
        children.push_back(std::move(valueVar));
        return children;
    }

    // --- Generic Product (records, FileInfo, etc.) ---
    if (obj->type->kind == CoreVM::TypeKind::Product && !obj->type->fields.empty())
    {
        for (auto const& field: obj->type->fields)
        {
            auto const val = obj->getSlot(field.offset);
            Variable child;
            child.name = field.name;
            child.value = builtins::slotValueToString(val, field.type, _runner.get());
            child.variablesReference = maybeAllocateChildRef(val);
            if (field.type == CoreVM::LiteralType::Void || field.type == CoreVM::LiteralType::Object)
                child.type = field.nestedTypeName.empty() ? "object" : field.nestedTypeName;
            else
                child.type = literalTypeName(field.type);
            children.push_back(std::move(child));
        }
        return children;
    }

    // --- Generic Sum (user ADTs) ---
    if (obj->type->kind == CoreVM::TypeKind::Sum)
    {
        auto const* variant = obj->type->getVariant(obj->tag);
        if (variant)
        {
            Variable variantVar;
            variantVar.name = "variant";
            variantVar.value = variant->name;
            variantVar.type = "string";
            variantVar.variablesReference = 0;
            children.push_back(std::move(variantVar));

            for (size_t i = 0; i < variant->fields.size(); ++i)
            {
                auto const& field = variant->fields[i];
                auto const val = obj->getSlot(field.offset);
                Variable child;
                child.name = field.name.empty() ? "[" + std::to_string(i) + "]" : field.name;
                child.value = builtins::slotValueToString(val, field.type, _runner.get());
                child.variablesReference = maybeAllocateChildRef(val);
                child.type = literalTypeName(field.type);
                children.push_back(std::move(child));
            }
        }
        return children;
    }

    return children;
}

void DebugSession::stopExecution(StopReason reason, size_t /*ip*/)
{
    resetVarRefMap();
    _stopReason = reason;
    _runner->suspend();

    nlohmann::json body = {
        { "reason", stopReasonToString(reason) },
        { "threadId", 1 },
        { "allThreadsStopped", true },
    };

    if (reason == StopReason::Breakpoint && !_hitBreakpointIds.empty())
        body["hitBreakpointIds"] = _hitBreakpointIds;

    _eventSender("stopped", std::move(body));
}

void DebugSession::onTrace(CoreVM::Instruction /*instr*/, size_t ip, size_t /*sp*/)
{
    // 1. Pause request — highest priority
    if (_pauseRequested)
    {
        _pauseRequested = false;
        stopExecution(StopReason::Pause, ip);
        return;
    }

    // Get source location for current instruction
    auto const& loc = _runner->function()->locationOf(ip);
    if (loc.filename.empty())
        return;

    auto const line = static_cast<int>(loc.begin.line);

    // 2. stopOnEntry — first instruction only
    if (_stopOnEntry)
    {
        _stopOnEntry = false;
        _lastStopFile = loc.filename;
        _lastStopLine = line;
        stopExecution(StopReason::Entry, ip);
        return;
    }

    // 3. Instruction breakpoints (address-based, checked before step/line breakpoints)
    {
        auto const funcIdx = _program->indexOf(_runner->function());
        if (funcIdx >= 0)
        {
            auto const packedAddr =
                (static_cast<uint64_t>(funcIdx) << 32) | static_cast<uint64_t>(ip & 0xFFFFFFFF);
            if (_breakpointManager.shouldStopAtInstruction(packedAddr))
            {
                _hitBreakpointIds = _breakpointManager.hitInstructionBreakpointIds(packedAddr);
                _lastStopFile = loc.filename;
                _lastStopLine = line;
                stopExecution(StopReason::Breakpoint, ip);
                return;
            }
        }
    }

    // 4. Step logic
    if (_stepMode != StepMode::None)
    {
        // For instruction granularity, stop on every instruction (skip line-change check)
        if (_steppingGranularity == SteppingGranularity::Instruction)
        {
            _stepMode = StepMode::None;
            _lastStopFile = loc.filename;
            _lastStopLine = line;
            stopExecution(StopReason::Step, ip);
            return;
        }

        // Skip if same as last stopped location
        if (loc.filename == _lastStopFile && line == _lastStopLine)
            return;

        auto const currentDepth = _runner->callStackDepth();
        bool shouldStop = false;

        switch (_stepMode)
        {
            case StepMode::StepOver:
                shouldStop = currentDepth <= _stepStartDepth && line != _stepStartLine;
                break;
            case StepMode::StepIn:
                shouldStop = (line != _stepStartLine || loc.filename != _stepStartFile);
                break;
            case StepMode::StepOut: shouldStop = currentDepth < _stepStartDepth; break;
            case StepMode::None: break;
        }

        if (shouldStop)
        {
            _stepMode = StepMode::None;
            _lastStopFile = loc.filename;
            _lastStopLine = line;
            stopExecution(StopReason::Step, ip);
            return;
        }

        // When stepping, skip breakpoint checks to avoid double-stops
        return;
    }

    // 5. Source/function breakpoints
    if (!_breakpointManager.hasBreakpoints())
        return;

    // Skip if same as last stopped location (avoid re-stopping on multi-instruction lines)
    if (loc.filename == _lastStopFile && line == _lastStopLine)
        return;

    if (!_breakpointManager.shouldStop(loc.filename, line))
        return;

    // Evaluate conditions, hit counts, and log messages
    size_t fp = _runner->framePointer();
    size_t funcId = 0;
    auto const funcIdx = _program->indexOf(_runner->function());
    if (funcIdx >= 0)
        funcId = static_cast<size_t>(funcIdx);

    auto check = _breakpointManager.checkStop(loc.filename, line, _runner.get(), _program.get(), fp, funcId);

    // Log points emit output but don't stop
    if (check.isLogPoint && check.logMessage.has_value())
    {
        _eventSender("output", { { "category", "console" }, { "output", *check.logMessage + "\n" } });
        if (!check.shouldStop)
            return;
    }

    if (!check.shouldStop)
        return;

    // Breakpoint hit
    _hitBreakpointIds = std::move(check.hitBreakpointIds);
    _lastStopFile = loc.filename;
    _lastStopLine = line;
    stopExecution(StopReason::Breakpoint, ip);
}

bool DebugSession::handleRunResult(bool exitNonZero)
{
    if (_runner->state() == CoreVM::Runner::State::Suspended)
    {
        // VM suspended — not terminated
        return false;
    }

    // Execution completed
    _terminated = true;
    auto const exitCode = exitNonZero ? 1 : 0;
    _eventSender("terminated", nlohmann::json::object());
    _eventSender("exited", { { "exitCode", exitCode } });
    return true;
}

std::vector<StackFrame> DebugSession::getStackTrace(int startFrame, int levels) const
{
    std::vector<StackFrame> frames;
    if (!_runner)
        return frames;

    // Frame 0: current execution point
    auto const* currentFn = _runner->function();
    auto const currentIp = _runner->getInstructionPointer();
    auto const& currentLoc = currentFn->locationOf(currentIp);

    struct FrameInfo
    {
        std::string name;
        std::string file;
        int line;
        int column;
        size_t ip;
        size_t funcIndex;
    };

    std::vector<FrameInfo> allFrames;
    allFrames.push_back({ .name = currentFn->name(),
                          .file = currentLoc.filename,
                          .line = static_cast<int>(currentLoc.begin.line),
                          .column = static_cast<int>(currentLoc.begin.column),
                          .ip = currentIp,
                          .funcIndex = static_cast<size_t>(std::max(0, _program->indexOf(currentFn))) });

    // Walk the call stack from back (most recent) to front
    auto const callStack = _runner->callStack();
    for (auto const& frame: std::ranges::reverse_view(callStack))
    {
        if (!frame.function)
            continue;
        auto const& loc = frame.function->locationOf(frame.ip);
        allFrames.push_back(
            { .name = frame.function->name(),
              .file = loc.filename,
              .line = static_cast<int>(loc.begin.line),
              .column = static_cast<int>(loc.begin.column),
              .ip = frame.ip,
              .funcIndex = static_cast<size_t>(std::max(0, _program->indexOf(frame.function))) });
    }

    // Apply pagination
    auto const totalFrames = static_cast<int>(allFrames.size());
    auto const start = std::min(startFrame, totalFrames);
    auto const end = (levels > 0) ? std::min(start + levels, totalFrames) : totalFrames;

    for (auto i = start; i < end; ++i)
    {
        auto const& fi = allFrames[i];
        StackFrame sf;
        sf.id = i;
        sf.name = fi.name;
        sf.line = fi.line;
        sf.column = fi.column;
        sf.instructionPointerReference = encodeAddress(fi.funcIndex, fi.ip);
        if (!fi.file.empty())
        {
            sf.source.path = fi.file;
            // Extract just the filename from the path
            auto const pos = fi.file.find_last_of('/');
            sf.source.name = (pos != std::string::npos) ? fi.file.substr(pos + 1) : fi.file;
        }
        frames.push_back(std::move(sf));
    }

    return frames;
}

std::vector<Scope> DebugSession::getScopes(int frameId) const
{
    std::vector<Scope> scopes;

    // Provide a "Locals" scope with variablesReference encoding the frame
    Scope locals;
    locals.name = "Locals";
    locals.variablesReference = ((frameId + 1) * 1000) + 1;
    locals.expensive = false;
    scopes.push_back(std::move(locals));

    // Provide a "Globals" scope if @main has debug variables.
    // In endo, top-level `let` bindings live in @main — they serve as globals.
    auto const* mainFn = _program->findFunction("@main");
    if (mainFn)
    {
        auto const mainIdx = _program->indexOf(mainFn);
        if (mainIdx >= 0
            && !_program->constants().getFunctionDebugVarInfo(static_cast<size_t>(mainIdx)).empty())
        {
            Scope globals;
            globals.name = "Globals";
            globals.variablesReference = ((frameId + 1) * 1000) + 2;
            globals.expensive = false;
            scopes.push_back(std::move(globals));
        }
    }

    return scopes;
}

std::vector<Variable> DebugSession::getVariables(int variablesReference) const
{
    std::vector<Variable> vars;
    if (!_runner)
        return vars;

    // Handle expanded variable references (children of structured types)
    if (variablesReference >= 100000)
        return enumerateChildren(variablesReference);

    // Decode frameId from variablesReference: ref = (frameId+1)*1000 + scopeType
    auto const frameId = (variablesReference / 1000) - 1;
    auto const scopeType = variablesReference % 1000;

    // Handle globals scope — @main's variables serve as globals in endo
    if (scopeType == 2)
    {
        auto const* mainFn = _program->findFunction("@main");
        if (!mainFn)
            return vars;

        auto const mainIdx = _program->indexOf(mainFn);
        if (mainIdx < 0)
            return vars;

        // Find @main's frame pointer from the call stack
        size_t mainFp = 0; // @main is always at fp=0 (bottom of stack)
        if (_runner->function() == mainFn)
        {
            mainFp = _runner->framePointer();
        }
        else
        {
            // Walk call stack to find @main's frame
            auto const callStack = _runner->callStack();
            for (auto const& frame: callStack)
            {
                if (frame.function == mainFn)
                {
                    mainFp = frame.fp;
                    break;
                }
            }
        }

        auto const& debugVars = _program->constants().getFunctionDebugVarInfo(static_cast<size_t>(mainIdx));
        for (auto const& dvi: debugVars)
        {
            Variable var;
            var.name = dvi.name;
            var.variablesReference = 0;

            auto const stackIndex = mainFp + dvi.allocaIndex;
            if (stackIndex < _runner->getStackPointer())
            {
                auto const rawValue = _runner->stack()[stackIndex];
                var.value =
                    builtins::slotValueToString(rawValue, dvi.type, _runner.get(), /*quoteStrings=*/true);

                switch (dvi.type)
                {
                    case CoreVM::LiteralType::Number: var.type = "number"; break;
                    case CoreVM::LiteralType::Boolean: var.type = "boolean"; break;
                    case CoreVM::LiteralType::String: var.type = "string"; break;
                    case CoreVM::LiteralType::Float: var.type = "float"; break;
                    default:
                        if (_runner->isKnownObject(rawValue))
                        {
                            auto* obj =
                                reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawValue));
                            var.type = obj->type->name;
                            if (isExpandable(obj))
                                var.variablesReference = allocateVarRef(rawValue, obj);
                        }
                        else if (_runner->isKnownString(rawValue))
                        {
                            var.type = "string";
                        }
                        else
                        {
                            var.type = "object";
                        }
                        break;
                }
            }
            else
            {
                var.value = "<unavailable>";
                var.type = "";
            }

            vars.push_back(std::move(var));
        }
        return vars;
    }

    // Determine which function corresponds to this frame
    CoreVM::Function const* fn = nullptr;
    size_t fp = 0;

    if (frameId == 0)
    {
        // Current frame
        fn = _runner->function();
        fp = _runner->framePointer();
    }
    else
    {
        // Walk call stack
        auto const callStack = _runner->callStack();
        auto const stackIndex = static_cast<int>(callStack.size()) - frameId;
        if (stackIndex >= 0 && std::cmp_less(stackIndex, callStack.size()))
        {
            fn = callStack[stackIndex].function;
            fp = callStack[stackIndex].fp;
        }
    }

    if (!fn)
        return vars;

    // Get the function's ID in the program
    auto const funcId = _program->indexOf(fn);
    if (funcId < 0)
        return vars;

    // Get debug var info for this function
    auto const& debugVars = _program->constants().getFunctionDebugVarInfo(static_cast<size_t>(funcId));

    for (auto const& dvi: debugVars)
    {
        Variable var;
        var.name = dvi.name;
        var.variablesReference = 0; // Not expandable for now

        // Read value from stack at fp + allocaIndex
        auto const stackIndex = fp + dvi.allocaIndex;
        if (stackIndex < _runner->getStackPointer())
        {
            auto const rawValue = _runner->stack()[stackIndex];
            // Format value using the canonical shared formatter
            var.value = builtins::slotValueToString(rawValue, dvi.type, _runner.get(), /*quoteStrings=*/true);

            // Determine display type name
            switch (dvi.type)
            {
                case CoreVM::LiteralType::Number: var.type = "number"; break;
                case CoreVM::LiteralType::Boolean: var.type = "boolean"; break;
                case CoreVM::LiteralType::String: var.type = "string"; break;
                case CoreVM::LiteralType::Float: var.type = "float"; break;
                default:
                    // Infer type from runtime value
                    if (_runner->isKnownObject(rawValue))
                    {
                        auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawValue));
                        var.type = obj->type->name;
                        if (isExpandable(obj))
                            var.variablesReference = allocateVarRef(rawValue, obj);
                    }
                    else if (_runner->isKnownString(rawValue))
                    {
                        var.type = "string";
                    }
                    else
                    {
                        var.type = "object";
                    }
                    break;
            }
        }
        else
        {
            var.value = "<unavailable>";
            var.type = "";
        }

        vars.push_back(std::move(var));
    }

    return vars;
}

std::optional<EvaluateResult> DebugSession::evaluate(std::string const& expression,
                                                     int frameId,
                                                     std::string const& /*context*/) const
{
    if (!_runner)
        return std::nullopt;

    // Resolve frame for variable lookup
    CoreVM::Function const* fn = nullptr;
    size_t fp = 0;

    if (frameId == 0)
    {
        fn = _runner->function();
        fp = _runner->framePointer();
    }
    else
    {
        auto const callStack = _runner->callStack();
        auto const stackIndex = static_cast<int>(callStack.size()) - frameId;
        if (stackIndex >= 0 && std::cmp_less(stackIndex, callStack.size()))
        {
            fn = callStack[stackIndex].function;
            fp = callStack[stackIndex].fp;
        }
    }

    if (!fn)
        return std::nullopt;

    auto const funcIdx = _program->indexOf(fn);
    if (funcIdx < 0)
        return std::nullopt;

    auto const funcId = static_cast<size_t>(funcIdx);

    // Try ConditionEvaluator for expressions (arithmetic, comparisons, variables)
    auto const evalResult = ConditionEvaluator::evaluateToString(expression, *_runner, *_program, fp, funcId);
    if (evalResult.has_value())
    {
        EvaluateResult result;
        result.result = *evalResult;
        result.variablesReference = 0;

        // Determine type from result content
        auto const& debugVars = _program->constants().getFunctionDebugVarInfo(funcId);
        for (auto const& dvi: debugVars)
        {
            if (dvi.name == expression)
            {
                switch (dvi.type)
                {
                    case CoreVM::LiteralType::Number: result.type = "number"; break;
                    case CoreVM::LiteralType::Boolean: result.type = "boolean"; break;
                    case CoreVM::LiteralType::String: result.type = "string"; break;
                    case CoreVM::LiteralType::Float: result.type = "float"; break;
                    default: {
                        auto const stackIdx = fp + dvi.allocaIndex;
                        if (stackIdx < _runner->getStackPointer())
                        {
                            auto const rawValue = _runner->stack()[stackIdx];
                            if (_runner->isKnownObject(rawValue))
                            {
                                auto* obj =
                                    reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawValue));
                                result.type = obj->type->name;
                                if (isExpandable(obj))
                                    result.variablesReference = allocateVarRef(rawValue, obj);
                            }
                            else
                            {
                                result.type = "object";
                            }
                        }
                        else
                        {
                            result.type = "object";
                        }
                        break;
                    }
                }
                return result;
            }
        }
        // For complex expressions, try to infer type
        if (*evalResult == "true" || *evalResult == "false")
            result.type = "boolean";
        else
            result.type = "number";
        return result;
    }

    return std::nullopt;
}

bool DebugSession::handleRunResultWithError(CoreVM::Runner::RunResult result)
{
    if (_runner->state() == CoreVM::Runner::State::Suspended)
    {
        // Check if we suspended due to an exception
        if (_currentException.has_value())
        {
            _stopReason = StopReason::Exception;
            nlohmann::json body = {
                { "reason", "exception" },
                { "threadId", 1 },
                { "text", _currentException->message },
                { "allThreadsStopped", true },
            };
            _eventSender("stopped", std::move(body));
        }
        return false;
    }

    // Execution completed or errored
    _terminated = true;
    auto const exitCode = (!result.has_value() || result.value()) ? 1 : 0;

    if (!result.has_value())
    {
        // Runtime error — emit output event with the error
        _eventSender("output", { { "category", "stderr" }, { "output", result.error().format() + "\n" } });
    }

    _eventSender("terminated", nlohmann::json::object());
    _eventSender("exited", { { "exitCode", exitCode } });
    return true;
}

void DebugSession::setExceptionFilters(std::vector<std::string> const& filters)
{
    _exceptionFilters.clear();
    for (auto const& f: filters)
        _exceptionFilters.insert(f);

    // Update error callback if runner exists
    if (_runner)
    {
        if (!_exceptionFilters.empty())
        {
            _runner->setErrorCallback([this](CoreVM::RuntimeError const& error) -> bool {
                if (_exceptionFilters.contains("all") || _exceptionFilters.contains("runtime-error"))
                {
                    _currentException = error;
                    return true;
                }
                return false;
            });
        }
        else
        {
            _runner->setErrorCallback(nullptr);
        }
    }
}

std::optional<CoreVM::RuntimeError> DebugSession::getExceptionInfo() const
{
    return _currentException;
}

std::optional<Variable> DebugSession::setVariable(int variablesReference,
                                                  std::string const& name,
                                                  std::string const& value)
{
    if (!_runner)
        return std::nullopt;

    // Decode frameId and scopeType from variablesReference
    auto const frameId = (variablesReference / 1000) - 1;
    auto const scopeType = variablesReference % 1000;

    // Handle setting global variables (@main's variables serve as globals)
    if (scopeType == 2)
    {
        auto const* mainFn = _program->findFunction("@main");
        if (!mainFn)
            return std::nullopt;

        auto const mainIdx = _program->indexOf(mainFn);
        if (mainIdx < 0)
            return std::nullopt;

        // Find @main's frame pointer
        size_t mainFp = 0;
        if (_runner->function() == mainFn)
        {
            mainFp = _runner->framePointer();
        }
        else
        {
            auto const callStack = _runner->callStack();
            for (auto const& frame: callStack)
            {
                if (frame.function == mainFn)
                {
                    mainFp = frame.fp;
                    break;
                }
            }
        }

        auto const& debugVars = _program->constants().getFunctionDebugVarInfo(static_cast<size_t>(mainIdx));
        for (auto const& dvi: debugVars)
        {
            if (dvi.name != name)
                continue;

            auto const stackIndex = mainFp + dvi.allocaIndex;
            if (stackIndex >= _runner->getStackPointer())
                return std::nullopt;

            Variable var;
            var.name = name;
            var.variablesReference = 0;

            switch (dvi.type)
            {
                case CoreVM::LiteralType::Number: {
                    CoreVM::CoreNumber numVal = 0;
                    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), numVal);
                    if (ec != std::errc())
                        return std::nullopt;
                    _runner->mutableStack()[stackIndex] = static_cast<uint64_t>(numVal);
                    var.value = std::format("{}", numVal);
                    var.type = "number";
                    break;
                }
                case CoreVM::LiteralType::Boolean: {
                    auto const boolVal = (value == "true" || value == "1");
                    _runner->mutableStack()[stackIndex] = boolVal ? 1u : 0u;
                    var.value = boolVal ? "true" : "false";
                    var.type = "boolean";
                    break;
                }
                case CoreVM::LiteralType::Float: {
                    double floatVal = 0.0;
                    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), floatVal);
                    if (ec != std::errc())
                        return std::nullopt;
                    _runner->mutableStack()[stackIndex] = std::bit_cast<uint64_t>(floatVal);
                    var.value = std::format("{}", floatVal);
                    var.type = "float";
                    break;
                }
                case CoreVM::LiteralType::String: {
                    auto* str = _runner->newString(value);
                    _runner->mutableStack()[stackIndex] = reinterpret_cast<uint64_t>(str);
                    var.value = "\"" + value + "\"";
                    var.type = "string";
                    break;
                }
                default: return std::nullopt;
            }
            return var;
        }
        return std::nullopt;
    }

    CoreVM::Function const* fn = nullptr;
    size_t fp = 0;

    if (frameId == 0)
    {
        fn = _runner->function();
        fp = _runner->framePointer();
    }
    else
    {
        auto const callStack = _runner->callStack();
        auto const stackIndex = static_cast<int>(callStack.size()) - frameId;
        if (stackIndex >= 0 && std::cmp_less(stackIndex, callStack.size()))
        {
            fn = callStack[stackIndex].function;
            fp = callStack[stackIndex].fp;
        }
    }

    if (!fn)
        return std::nullopt;

    auto const funcIdx = _program->indexOf(fn);
    if (funcIdx < 0)
        return std::nullopt;

    auto const& debugVars = _program->constants().getFunctionDebugVarInfo(static_cast<size_t>(funcIdx));

    for (auto const& dvi: debugVars)
    {
        if (dvi.name != name)
            continue;

        auto const stackIndex = fp + dvi.allocaIndex;
        if (stackIndex >= _runner->getStackPointer())
            return std::nullopt;

        // Parse and write value based on type
        Variable var;
        var.name = name;
        var.variablesReference = 0;

        switch (dvi.type)
        {
            case CoreVM::LiteralType::Number: {
                CoreVM::CoreNumber numVal = 0;
                auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), numVal);
                if (ec != std::errc())
                    return std::nullopt;
                _runner->mutableStack()[stackIndex] = static_cast<uint64_t>(numVal);
                var.value = std::format("{}", numVal);
                var.type = "number";
                break;
            }
            case CoreVM::LiteralType::Boolean: {
                auto const boolVal = (value == "true" || value == "1");
                _runner->mutableStack()[stackIndex] = boolVal ? 1u : 0u;
                var.value = boolVal ? "true" : "false";
                var.type = "boolean";
                break;
            }
            case CoreVM::LiteralType::Float: {
                double floatVal = 0.0;
                auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), floatVal);
                if (ec != std::errc())
                    return std::nullopt;
                _runner->mutableStack()[stackIndex] = std::bit_cast<uint64_t>(floatVal);
                var.value = std::format("{}", floatVal);
                var.type = "float";
                break;
            }
            case CoreVM::LiteralType::String: {
                auto* str = _runner->newString(value);
                _runner->mutableStack()[stackIndex] = reinterpret_cast<uint64_t>(str);
                var.value = "\"" + value + "\"";
                var.type = "string";
                break;
            }
            default: return std::nullopt; // Can't set object variables this way
        }

        return var;
    }

    return std::nullopt;
}

std::vector<nlohmann::json> DebugSession::disassemble(std::string const& memoryReference,
                                                      int instructionCount,
                                                      int instructionOffset) const
{
    std::vector<nlohmann::json> instructions;

    // Parse hex-encoded memory reference
    if (!memoryReference.starts_with("0x") && !memoryReference.starts_with("0X"))
        return instructions;

    auto const [funcIndex, offset] = decodeAddress(memoryReference);

    auto const& functions = _program->constants().getFunctions();
    if (funcIndex >= functions.size())
        return instructions;

    auto const* fn = _program->function(funcIndex);
    if (!fn)
        return instructions;

    auto const& code = fn->code();
    auto const startOffset = static_cast<int>(offset) + instructionOffset;
    auto const endOffset = startOffset + instructionCount;

    for (auto i = std::max(0, startOffset); i < std::min(endOffset, static_cast<int>(code.size())); ++i)
    {
        auto const ip = static_cast<size_t>(i);
        auto const text = CoreVM::disassemble(code[ip], ip, 0, &_program->constants());
        auto const& loc = fn->locationOf(ip);

        nlohmann::json instr = {
            { "address", encodeAddress(funcIndex, ip) },
            { "instruction", text },
        };

        if (!loc.filename.empty())
        {
            instr["location"] = nlohmann::json {
                { "path", loc.filename },
            };
            instr["line"] = static_cast<int>(loc.begin.line);
            instr["column"] = static_cast<int>(loc.begin.column);
        }

        instructions.push_back(std::move(instr));
    }

    return instructions;
}

std::optional<std::string> DebugSession::getSourceContent(std::string const& path) const
{
    // Security boundary: only serve files that are part of the loaded program
    if (std::ranges::find(_loadedSources, path) == _loadedSources.end())
        return std::nullopt;

    std::ifstream file(path);
    if (!file)
        return std::nullopt;

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::vector<CompletionItem> DebugSession::getCompletions(std::string const& text, int frameId) const
{
    std::vector<CompletionItem> items;
    if (!_runner)
        return items;

    // Add variable names from current frame's locals
    auto const localsRef = ((frameId + 1) * 1000) + 1;
    auto const localVars = getVariables(localsRef);
    for (auto const& var: localVars)
    {
        if (text.empty() || var.name.starts_with(text))
            items.push_back({ .label = var.name, .type = "variable" });
    }

    // Add global variable names
    auto const globalsRef = ((frameId + 1) * 1000) + 2;
    auto const globalVars = getVariables(globalsRef);
    for (auto const& var: globalVars)
    {
        if (text.empty() || var.name.starts_with(text))
            items.push_back({ .label = var.name, .type = "variable" });
    }

    // Add function names from the program
    auto const funcNames = _program->functionNames();
    for (auto const& name: funcNames)
    {
        // Skip internal names (prefixed with @)
        if (name.starts_with("@"))
            continue;
        if (text.empty() || name.starts_with(text))
            items.push_back({ .label = name, .type = "function" });
    }

    return items;
}

} // namespace endo::dap
