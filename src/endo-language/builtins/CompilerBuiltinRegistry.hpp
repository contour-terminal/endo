// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file CompilerBuiltinRegistry.hpp
/// @brief Single source of truth for compiler-recognized builtin metadata.
///
/// Centralizes the dispatch properties of builtins that participate in multiple
/// compiler phases (parser statement dispatch, primary exclusion, unit-producing
/// detection). Adding a new builtin requires adding one entry to the table below,
/// then implementing its codegen in the appropriate visitor(s).
///
/// All queries are constexpr — zero runtime overhead.

#include <array>
#include <cstdint>
#include <string_view>

namespace endo
{

/// How the parser handles this builtin at statement level in parseStmt().
enum class StmtParseStrategy : uint8_t
{
    None,               ///< Not recognized at statement level (parsed as normal identifier)
    FSharpSimple,       ///< Enter F#, parseFSharpApplication(), wrap as ExprStmt
    FSharpWithPipeline, ///< Like Simple, plus optional trailing |> pipeline with displayResult
    ExecPipeline,       ///< Enter F#, parseExecPipeline(), optional trailing |> pipeline
};

/// Compile-time descriptor for a compiler-recognized builtin.
struct CompilerBuiltinEntry
{
    std::string_view name;
    StmtParseStrategy stmtStrategy = StmtParseStrategy::None;
    bool excludeFromPrimary = false;
    bool unitProducing = false;
};

// clang-format off

/// The authoritative table of compiler-recognized builtins.
///
/// To add a new builtin:
/// 1. Add an entry here with the correct flags.
/// 2. Implement codegen in the appropriate visitor(s).
/// 3. Parser dispatch and isUnitProducing checks will automatically pick it up.
inline constexpr std::array compilerBuiltins = {
    CompilerBuiltinEntry { .name = "print",              .stmtStrategy = StmtParseStrategy::FSharpSimple },
    CompilerBuiltinEntry { .name = "println",            .stmtStrategy = StmtParseStrategy::FSharpSimple },
    CompilerBuiltinEntry { .name = "each",               .stmtStrategy = StmtParseStrategy::FSharpSimple },
    CompilerBuiltinEntry { .name = "ignore",             .stmtStrategy = StmtParseStrategy::FSharpSimple,       .excludeFromPrimary = true, .unitProducing = true },
    CompilerBuiltinEntry { .name = "rand",               .stmtStrategy = StmtParseStrategy::FSharpWithPipeline },
    CompilerBuiltinEntry { .name = "time",               .stmtStrategy = StmtParseStrategy::FSharpWithPipeline },
    CompilerBuiltinEntry { .name = "register_completer", .stmtStrategy = StmtParseStrategy::FSharpSimple },
    CompilerBuiltinEntry { .name = "exec",               .stmtStrategy = StmtParseStrategy::ExecPipeline,       .excludeFromPrimary = true },
    CompilerBuiltinEntry { .name = "force" },
};

// clang-format on

// Compile-time uniqueness check
static_assert(
    []() constexpr {
        for (size_t i = 0; i < compilerBuiltins.size(); ++i)
            for (size_t j = i + 1; j < compilerBuiltins.size(); ++j)
                if (compilerBuiltins[i].name == compilerBuiltins[j].name)
                    return false;
        return true;
    }(),
    "Duplicate entry in compilerBuiltins");

/// Returns the statement parse strategy for a builtin, or None if not a compiler builtin.
constexpr auto getStmtParseStrategy(std::string_view name) -> StmtParseStrategy
{
    for (auto const& entry: compilerBuiltins)
        if (entry.name == name)
            return entry.stmtStrategy;
    return StmtParseStrategy::None;
}

/// Returns true if this name should be excluded from isFSharpPrimary().
constexpr auto isExcludedFromPrimary(std::string_view name) -> bool
{
    for (auto const& entry: compilerBuiltins)
        if (entry.name == name)
            return entry.excludeFromPrimary;
    return false;
}

/// Returns true if this builtin is statically known to produce unit.
constexpr auto isUnitProducingBuiltin(std::string_view name) -> bool
{
    for (auto const& entry: compilerBuiltins)
        if (entry.name == name)
            return entry.unitProducing;
    return false;
}

} // namespace endo
