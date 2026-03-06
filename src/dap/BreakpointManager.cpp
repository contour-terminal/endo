// SPDX-License-Identifier: Apache-2.0
#include "BreakpointManager.hpp"

#include <CoreVM/vm/Program.hpp>

#include <algorithm>
#include <charconv>
#include <functional>
#include <ranges>

#include "ConditionEvaluator.hpp"

namespace endo::dap
{

std::vector<Breakpoint> BreakpointManager::setSourceBreakpoints(
    std::string const& path, std::vector<SourceBreakpoint> const& breakpoints, CoreVM::Program const* program)
{
    // Remove old breakpoints for this path
    _sourceBreakpoints.erase(path);

    std::vector<Breakpoint> result;
    std::vector<ResolvedBreakpoint> resolved;

    for (auto const& bp: breakpoints)
    {
        auto const id = _nextId++;

        ResolvedBreakpoint rb;
        rb.id = id;
        rb.sourcePath = path;
        rb.requestedLine = bp.line;
        rb.condition = bp.condition;
        rb.hitCondition = bp.hitCondition;
        rb.logMessage = bp.logMessage;

        // Resolve against location tables if program is available
        if (program)
        {
            auto const funcNames = program->functionNames();
            int bestLine = -1;
            int bestColumn = 1;

            for (auto const& funcName: funcNames)
            {
                auto const* fn = program->findFunction(funcName);
                if (!fn)
                    continue;

                auto const funcIdx = program->indexOf(fn);
                if (funcIdx < 0)
                    continue;

                auto const& locTable =
                    program->constants().getFunctionLocationTable(static_cast<size_t>(funcIdx));

                for (auto const& [offset, loc]: locTable)
                {
                    if (loc.filename != path)
                        continue;

                    auto const locLine = static_cast<int>(loc.begin.line);
                    if (locLine < bp.line)
                        continue;

                    // Find closest line >= requested line
                    if (bestLine < 0 || locLine < bestLine)
                    {
                        bestLine = locLine;
                        bestColumn = static_cast<int>(loc.begin.column);
                    }
                }
            }

            if (bestLine >= 0)
            {
                rb.resolvedLine = bestLine;
                rb.resolvedColumn = bestColumn;
                rb.verified = true;
            }
            else
            {
                rb.resolvedLine = bp.line;
                rb.resolvedColumn = 1;
                rb.verified = false;
            }
        }
        else
        {
            rb.resolvedLine = bp.line;
            rb.resolvedColumn = 1;
            rb.verified = false;
        }

        resolved.push_back(rb);

        Breakpoint response;
        response.id = id;
        response.verified = rb.verified;
        response.sourcePath = path;
        response.line = rb.resolvedLine;
        response.column = rb.resolvedColumn;
        result.push_back(std::move(response));
    }

    if (!resolved.empty())
        _sourceBreakpoints[path] = std::move(resolved);

    rebuildLookupSet();
    return result;
}

std::vector<Breakpoint> BreakpointManager::setFunctionBreakpoints(
    std::vector<FunctionBreakpoint> const& breakpoints, CoreVM::Program const* program)
{
    _functionBreakpoints.clear();

    std::vector<Breakpoint> result;

    for (auto const& fbp: breakpoints)
    {
        auto const id = _nextId++;

        ResolvedFunctionBreakpoint rfb;
        rfb.id = id;
        rfb.functionName = fbp.name;

        if (program)
        {
            auto const* fn = program->findFunction(fbp.name);
            if (fn)
            {
                rfb.verified = true;
                // Resolve to first instruction's location
                auto const& loc = fn->locationOf(0);
                if (!loc.filename.empty())
                {
                    rfb.sourcePath = loc.filename;
                    rfb.line = static_cast<int>(loc.begin.line);
                }
            }
        }

        _functionBreakpoints.push_back(rfb);

        Breakpoint response;
        response.id = id;
        response.verified = rfb.verified;
        if (!rfb.sourcePath.empty())
        {
            response.sourcePath = rfb.sourcePath;
            response.line = rfb.line;
        }
        result.push_back(std::move(response));
    }

    rebuildLookupSet();
    return result;
}

bool BreakpointManager::shouldStop(std::string const& filename, int line) const
{
    return _stopLocations.contains(makeKey(filename, line));
}

std::vector<int> BreakpointManager::hitBreakpointIds(std::string const& filename, int line) const
{
    std::vector<int> ids;

    for (auto const& [path, bps]: _sourceBreakpoints)
    {
        for (auto const& bp: bps)
        {
            if (bp.verified && bp.sourcePath == filename && bp.resolvedLine == line)
                ids.push_back(bp.id);
        }
    }

    for (auto const& fbp: _functionBreakpoints)
    {
        if (fbp.verified && fbp.sourcePath == filename && fbp.line == line)
            ids.push_back(fbp.id);
    }

    return ids;
}

std::vector<BreakpointLocation> BreakpointManager::breakpointLocations(std::string const& path,
                                                                       int startLine,
                                                                       int endLine,
                                                                       CoreVM::Program const& program)
{
    std::vector<BreakpointLocation> locations;
    std::unordered_set<int> seenLines;

    auto const funcNames = program.functionNames();
    for (auto const& funcName: funcNames)
    {
        auto const* fn = program.findFunction(funcName);
        if (!fn)
            continue;

        auto const funcIdx = program.indexOf(fn);
        if (funcIdx < 0)
            continue;

        auto const& locTable = program.constants().getFunctionLocationTable(static_cast<size_t>(funcIdx));

        for (auto const& [offset, loc]: locTable)
        {
            if (loc.filename != path)
                continue;

            auto const locLine = static_cast<int>(loc.begin.line);
            if (locLine < startLine || locLine > endLine)
                continue;

            if (seenLines.contains(locLine))
                continue;
            seenLines.insert(locLine);

            BreakpointLocation bl;
            bl.line = locLine;
            bl.column = static_cast<int>(loc.begin.column);
            bl.endLine = static_cast<int>(loc.end.line);
            bl.endColumn = static_cast<int>(loc.end.column);
            locations.push_back(bl);
        }
    }

    // Sort by line
    std::ranges::sort(locations, {}, &BreakpointLocation::line);
    return locations;
}

std::vector<Breakpoint> BreakpointManager::setInstructionBreakpoints(
    std::vector<InstructionBreakpoint> const& breakpoints)
{
    _instructionBreakpoints.clear();
    _instructionStopLocations.clear();

    std::vector<Breakpoint> result;

    for (auto const& ibp: breakpoints)
    {
        auto const id = _nextId++;

        // Parse the hex address
        uint64_t address = 0;
        bool parsed = false;
        if (ibp.instructionReference.starts_with("0x") || ibp.instructionReference.starts_with("0X"))
        {
            auto const hex = ibp.instructionReference.substr(2);
            auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), address, 16);
            parsed = (ec == std::errc());
        }

        // Apply offset
        if (ibp.offset.has_value())
            address += static_cast<uint64_t>(*ibp.offset);

        ResolvedInstructionBreakpoint rib;
        rib.id = id;
        rib.packedAddress = address;
        rib.verified = parsed;

        if (rib.verified)
            _instructionStopLocations.insert(address);

        _instructionBreakpoints.push_back(rib);

        Breakpoint response;
        response.id = id;
        response.verified = rib.verified;
        result.push_back(std::move(response));
    }

    return result;
}

bool BreakpointManager::shouldStopAtInstruction(uint64_t packedAddress) const
{
    return _instructionStopLocations.contains(packedAddress);
}

std::vector<int> BreakpointManager::hitInstructionBreakpointIds(uint64_t packedAddress) const
{
    std::vector<int> ids;
    for (auto const& ib: _instructionBreakpoints)
    {
        if (ib.verified && ib.packedAddress == packedAddress)
            ids.push_back(ib.id);
    }
    return ids;
}

bool BreakpointManager::hasBreakpoints() const noexcept
{
    return !_sourceBreakpoints.empty() || !_functionBreakpoints.empty() || !_instructionBreakpoints.empty();
}

void BreakpointManager::clearAll()
{
    _sourceBreakpoints.clear();
    _functionBreakpoints.clear();
    _stopLocations.clear();
    _instructionBreakpoints.clear();
    _instructionStopLocations.clear();
}

BreakpointManager::StopCheck BreakpointManager::checkStop(std::string const& filename,
                                                          int line,
                                                          CoreVM::Runner const* runner,
                                                          CoreVM::Program const* program,
                                                          size_t fp,
                                                          size_t funcId)
{
    StopCheck result;

    for (auto& [path, bps]: _sourceBreakpoints)
    {
        for (auto& bp: bps)
        {
            if (!bp.verified || bp.sourcePath != filename || bp.resolvedLine != line)
                continue;

            // Increment hit count
            ++bp.hitCount;

            // Evaluate condition if present
            if (bp.condition.has_value() && !bp.condition->empty() && runner && program)
            {
                if (!ConditionEvaluator::evaluate(*bp.condition, *runner, *program, fp, funcId))
                    continue;
            }

            // Check hit condition
            if (bp.hitCondition.has_value() && !bp.hitCondition->empty())
            {
                if (!ConditionEvaluator::checkHitCondition(*bp.hitCondition, bp.hitCount))
                    continue;
            }

            // Log point handling
            if (bp.logMessage.has_value() && !bp.logMessage->empty())
            {
                result.isLogPoint = true;
                if (runner && program)
                    result.logMessage = ConditionEvaluator::interpolateLogMessage(
                        *bp.logMessage, *runner, *program, fp, funcId);
                else
                    result.logMessage = *bp.logMessage;
                continue; // Log points don't stop
            }

            result.shouldStop = true;
            result.hitBreakpointIds.push_back(bp.id);
        }
    }

    // Function breakpoints (no conditions/hitcounts)
    for (auto const& fbp: _functionBreakpoints)
    {
        if (fbp.verified && fbp.sourcePath == filename && fbp.line == line)
        {
            result.shouldStop = true;
            result.hitBreakpointIds.push_back(fbp.id);
        }
    }

    return result;
}

void BreakpointManager::rebuildLookupSet()
{
    _stopLocations.clear();

    for (auto const& [path, bps]: _sourceBreakpoints)
    {
        for (auto const& bp: bps)
        {
            if (bp.verified)
                _stopLocations.insert(makeKey(bp.sourcePath, bp.resolvedLine));
        }
    }

    for (auto const& fbp: _functionBreakpoints)
    {
        if (fbp.verified)
            _stopLocations.insert(makeKey(fbp.sourcePath, fbp.line));
    }
}

uint64_t BreakpointManager::makeKey(std::string const& filename, int line)
{
    auto const pathHash = std::hash<std::string> {}(filename);
    return (static_cast<uint64_t>(pathHash) << 32) | static_cast<uint64_t>(static_cast<uint32_t>(line));
}

} // namespace endo::dap
