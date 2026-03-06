// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file BreakpointManager.hpp
/// @brief Manages source and function breakpoints for the DAP debug session.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DapTypes.hpp"

namespace CoreVM
{
class Program;
class Runner;
} // namespace CoreVM

namespace endo::dap
{

/// A resolved source breakpoint with verified status and resolved location.
struct ResolvedBreakpoint
{
    int id = 0;
    std::string sourcePath;
    int requestedLine = 0;
    int resolvedLine = 0;
    int resolvedColumn = 0;
    bool verified = false;
    std::optional<std::string> condition;
    std::optional<std::string> hitCondition;
    std::optional<std::string> logMessage;
    size_t hitCount = 0;
};

/// A resolved function breakpoint.
struct ResolvedFunctionBreakpoint
{
    int id = 0;
    std::string functionName;
    bool verified = false;
    std::string sourcePath;
    int line = 0;
};

/// Manages breakpoint storage, resolution, and fast lookup.
///
/// Pure data class with no DAP protocol or VM execution knowledge.
class BreakpointManager
{
  public:
    /// Replaces all source breakpoints for the given file path.
    /// @param path Source file path
    /// @param breakpoints Client-requested breakpoints
    /// @param program Compiled program for location table lookup (may be null)
    /// @return DAP Breakpoint responses with resolved locations
    std::vector<Breakpoint> setSourceBreakpoints(std::string const& path,
                                                 std::vector<SourceBreakpoint> const& breakpoints,
                                                 CoreVM::Program const* program);

    /// Replaces all function breakpoints.
    /// @param breakpoints Client-requested function breakpoints
    /// @param program Compiled program for function name lookup (may be null)
    /// @return DAP Breakpoint responses with resolved locations
    std::vector<Breakpoint> setFunctionBreakpoints(std::vector<FunctionBreakpoint> const& breakpoints,
                                                   CoreVM::Program const* program);

    /// Result of checking breakpoint stop conditions.
    struct StopCheck
    {
        bool shouldStop = false;
        bool isLogPoint = false;
        std::optional<std::string> logMessage;
        std::vector<int> hitBreakpointIds;
    };

    /// Fast O(1) check whether execution should stop at the given location.
    [[nodiscard]] bool shouldStop(std::string const& filename, int line) const;

    /// Returns the IDs of all breakpoints matching the given location.
    [[nodiscard]] std::vector<int> hitBreakpointIds(std::string const& filename, int line) const;

    /// Evaluates conditions, hit counts, and log messages for all breakpoints at a location.
    /// @param filename Source file path
    /// @param line Line number
    /// @param runner Current VM runner (for variable access in conditions)
    /// @param program Compiled program (for debug variable info)
    /// @param fp Frame pointer
    /// @param funcId Function index
    [[nodiscard]] StopCheck checkStop(std::string const& filename,
                                      int line,
                                      CoreVM::Runner const* runner,
                                      CoreVM::Program const* program,
                                      size_t fp,
                                      size_t funcId);

    /// Returns possible breakpoint locations within a source range.
    [[nodiscard]] static std::vector<BreakpointLocation> breakpointLocations(std::string const& path,
                                                                             int startLine,
                                                                             int endLine,
                                                                             CoreVM::Program const& program);

    /// Replaces all instruction breakpoints.
    /// @param breakpoints Client-requested instruction breakpoints
    /// @return DAP Breakpoint responses with resolved locations
    std::vector<Breakpoint> setInstructionBreakpoints(std::vector<InstructionBreakpoint> const& breakpoints);

    /// Fast O(1) check whether an instruction breakpoint exists at the given packed address.
    [[nodiscard]] bool shouldStopAtInstruction(uint64_t packedAddress) const;

    /// Returns the IDs of instruction breakpoints matching the given packed address.
    [[nodiscard]] std::vector<int> hitInstructionBreakpointIds(uint64_t packedAddress) const;

    /// Returns true if any breakpoints are currently set.
    [[nodiscard]] bool hasBreakpoints() const noexcept;

    /// Removes all breakpoints.
    void clearAll();

  private:
    /// Rebuilds the fast-path lookup set from current resolved breakpoints.
    void rebuildLookupSet();

    /// Packs a filename and line into a single key for the lookup set.
    [[nodiscard]] static uint64_t makeKey(std::string const& filename, int line);

    int _nextId = 1;

    /// Source breakpoints per file path.
    std::unordered_map<std::string, std::vector<ResolvedBreakpoint>> _sourceBreakpoints;

    /// Function breakpoints.
    std::vector<ResolvedFunctionBreakpoint> _functionBreakpoints;

    /// Fast-path O(1) lookup set keyed on (hash(filename) << 32 | line).
    std::unordered_set<uint64_t> _stopLocations;

    /// Resolved instruction breakpoints keyed by packed address.
    struct ResolvedInstructionBreakpoint
    {
        int id = 0;
        uint64_t packedAddress = 0;
        bool verified = false;
    };

    /// Instruction breakpoints storage.
    std::vector<ResolvedInstructionBreakpoint> _instructionBreakpoints;

    /// Fast-path O(1) lookup set for instruction breakpoints.
    std::unordered_set<uint64_t> _instructionStopLocations;
};

} // namespace endo::dap
