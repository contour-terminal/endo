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

#include <CoreVM/enums.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

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

// ---------------------------------------------------------------------------
// Phase 2: Codegen registry for data-driven builtin dispatch
// ---------------------------------------------------------------------------

/// Derives the arity (number of parameters) from a callback signature string.
/// Counts characters between '(' and ')'. e.g., "string_trim(S)S" → 1, "string_contains(SS)B" → 2.
constexpr auto arityFromSignature(std::string_view sig) -> int
{
    auto const open = sig.find('(');
    auto const close = sig.find(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
        return 0;
    return static_cast<int>(close - open - 1);
}

/// How result type annotations are propagated from input arguments.
enum class ResultPropagation : uint8_t
{
    None,                     ///< No annotation propagation
    ListElementAsOptionInner, ///< Propagate list element typeId as Option inner objectTypeId (head, last,
                              ///< nth)
    ListElementAsList,        ///< Propagate list element typeId+literalType to result list (tail)
};

/// Data-driven codegen descriptor for builtins dispatched via callback.
///
/// Used by both tryGenerateBuiltinCall (ApplicationExpr) and the PipelineExpr visitor.
/// A generic dispatcher evaluates args, finds the callback, calls it, and annotates the result.
struct BuiltinCallEntry
{
    std::string_view name;              ///< User-facing builtin name
    std::string_view callbackSignature; ///< VM callback signature, e.g. "string_trim(S)S"
    bool pipelineSupport = false;       ///< Whether this builtin works as a unary pipeline target
    bool reverseArgs = false;           ///< Pass args in reverse order (data-last convention)

    /// Static result annotations (applied unconditionally after the call).
    uint16_t resultObjectTypeId = 0;                                              ///< 0 = no annotation
    CoreVM::LiteralType resultInnerLiteralType = CoreVM::LiteralType::Void;       ///< Void = no annotation
    uint16_t resultInnerObjectTypeId = 0;                                         ///< 0 = no annotation
    CoreVM::LiteralType resultListElementLiteralType = CoreVM::LiteralType::Void; ///< Void = no annotation

    /// Dynamic propagation from input args.
    ResultPropagation propagation = ResultPropagation::None;

    /// Derives arity from callbackSignature.
    [[nodiscard]] constexpr auto arity() const -> int { return arityFromSignature(callbackSignature); }
};

// clang-format off

/// Data-driven builtin call table.
///
/// Builtins listed here are dispatched generically by tryDispatchBuiltinCall().
/// Builtins NOT listed here (env, time, rand, etc.) use custom codegen.
namespace detail
{
    inline constexpr auto List = CoreVM::BuiltinTypeId::List;
    inline constexpr auto Opt = CoreVM::BuiltinTypeId::Option;
    inline constexpr auto Res = CoreVM::BuiltinTypeId::Result;
    inline constexpr auto Md = CoreVM::BuiltinTypeId::Markdown;
    inline constexpr auto Num = CoreVM::LiteralType::Number;
    inline constexpr auto Str = CoreVM::LiteralType::String;
} // namespace detail

inline constexpr std::array builtinCallEntries = {
    // String operations
    BuiltinCallEntry { .name = "string_length",   .callbackSignature = "string_grapheme_length(S)I", .pipelineSupport = true },
    BuiltinCallEntry { .name = "grapheme_length",  .callbackSignature = "string_grapheme_length(S)I", .pipelineSupport = true },
    BuiltinCallEntry { .name = "byte_length",      .callbackSignature = "string_byte_length(S)I",     .pipelineSupport = true },
    BuiltinCallEntry { .name = "codepoint_length", .callbackSignature = "string_codepoint_length(S)I", .pipelineSupport = true },
    BuiltinCallEntry { .name = "bytes",       .callbackSignature = "string_to_bytes(S)I",     .pipelineSupport = true,
                        .resultObjectTypeId = detail::List, .resultListElementLiteralType = detail::Num },
    BuiltinCallEntry { .name = "codepoints",  .callbackSignature = "string_to_codepoints(S)I", .pipelineSupport = true,
                        .resultObjectTypeId = detail::List, .resultListElementLiteralType = detail::Num },
    BuiltinCallEntry { .name = "graphemes",   .callbackSignature = "string_to_graphemes(S)I", .pipelineSupport = true,
                        .resultObjectTypeId = detail::List, .resultListElementLiteralType = detail::Str },
    BuiltinCallEntry { .name = "trim",        .callbackSignature = "string_trim(S)S",     .pipelineSupport = true },
    BuiltinCallEntry { .name = "toLower",     .callbackSignature = "string_toLower(S)S",  .pipelineSupport = true },
    BuiltinCallEntry { .name = "toUpper",     .callbackSignature = "string_toUpper(S)S",  .pipelineSupport = true },
    BuiltinCallEntry { .name = "lines",       .callbackSignature = "string_lines(S)I",    .pipelineSupport = true,
                        .resultObjectTypeId = detail::List, .resultListElementLiteralType = detail::Str },
    BuiltinCallEntry { .name = "toText",      .callbackSignature = "object_to_string(I)S", .pipelineSupport = true },
    BuiltinCallEntry { .name = "replace",     .callbackSignature = "string_replace(SSS)S" },
    BuiltinCallEntry { .name = "join",        .callbackSignature = "string_join(SI)S" },

    // String predicates (data-last: args reversed)
    BuiltinCallEntry { .name = "contains",   .callbackSignature = "string_contains(SS)B",    .reverseArgs = true },
    BuiltinCallEntry { .name = "startsWith", .callbackSignature = "string_startsWith(SS)B",  .reverseArgs = true },
    BuiltinCallEntry { .name = "endsWith",   .callbackSignature = "string_endsWith(SS)B",    .reverseArgs = true },
    BuiltinCallEntry { .name = "split",      .callbackSignature = "string_split(SS)I",
                        .resultObjectTypeId = detail::List, .resultListElementLiteralType = detail::Str },

    // List operations
    BuiltinCallEntry { .name = "head",      .callbackSignature = "list_head(I)I",    .pipelineSupport = true,
                        .resultObjectTypeId = detail::Opt, .propagation = ResultPropagation::ListElementAsOptionInner },
    BuiltinCallEntry { .name = "tail",      .callbackSignature = "list_tail(I)I",    .pipelineSupport = true,
                        .resultObjectTypeId = detail::List, .propagation = ResultPropagation::ListElementAsList },
    BuiltinCallEntry { .name = "length",    .callbackSignature = "list_length(I)I",  .pipelineSupport = true },
    BuiltinCallEntry { .name = "isEmpty",   .callbackSignature = "list_isEmpty(I)B", .pipelineSupport = true },
    BuiltinCallEntry { .name = "last",      .callbackSignature = "list_last(I)I",    .pipelineSupport = true,
                        .resultObjectTypeId = detail::Opt, .propagation = ResultPropagation::ListElementAsOptionInner },
    BuiltinCallEntry { .name = "nth",       .callbackSignature = "list_nth(II)I",
                        .resultObjectTypeId = detail::Opt, .propagation = ResultPropagation::ListElementAsOptionInner },
    BuiltinCallEntry { .name = "replicate", .callbackSignature = "list_replicate(II)I",
                        .resultObjectTypeId = detail::List },

    // Lookup
    BuiltinCallEntry { .name = "which",   .callbackSignature = "which_find(S)I",   .pipelineSupport = true,
                        .resultObjectTypeId = detail::Opt, .resultInnerLiteralType = detail::Str },

    // Formatting
    BuiltinCallEntry { .name = "formatDateTime", .callbackSignature = "format_datetime(I)S" },
    BuiltinCallEntry { .name = "formatTimeSpan", .callbackSignature = "format_timespan(I)S", .pipelineSupport = true },
    BuiltinCallEntry { .name = "formatMode",     .callbackSignature = "format_mode(I)S" },
    BuiltinCallEntry { .name = "formatNumber",   .callbackSignature = "format_number(I)S",  .pipelineSupport = true },
    BuiltinCallEntry { .name = "formatNumber",   .callbackSignature = "format_number(SI)S" },

    // Mode checks
    BuiltinCallEntry { .name = "isReadable",   .callbackSignature = "mode_isReadable(I)B" },
    BuiltinCallEntry { .name = "isWritable",   .callbackSignature = "mode_isWritable(I)B" },
    BuiltinCallEntry { .name = "isExecutable", .callbackSignature = "mode_isExecutable(I)B" },

    // Misc
    BuiltinCallEntry { .name = "sleep",    .callbackSignature = "timespan_sleep(I)I", .pipelineSupport = true },
    BuiltinCallEntry { .name = "markdown", .callbackSignature = "markdown_create(S)I", .pipelineSupport = true,
                        .resultObjectTypeId = detail::Md },

    // Overloaded: fetch (1 or 2 args)
    BuiltinCallEntry { .name = "fetch", .callbackSignature = "fetch(S)I",
                        .resultObjectTypeId = detail::Res, .resultInnerLiteralType = detail::Str },
    BuiltinCallEntry { .name = "fetch", .callbackSignature = "fetch(SI)I",
                        .resultObjectTypeId = detail::Res, .resultInnerLiteralType = detail::Str },
};

// clang-format on

/// Looks up a builtin call entry by name and arity.
/// Returns nullptr if not found.
constexpr auto findBuiltinCallEntry(std::string_view name, int arity) -> BuiltinCallEntry const*
{
    for (auto const& entry: builtinCallEntries)
        if (entry.name == name && entry.arity() == arity)
            return &entry;
    return nullptr;
}

/// Looks up a builtin call entry by name (any arity).
/// Returns nullptr if not found.
constexpr auto findBuiltinCallEntryByName(std::string_view name) -> BuiltinCallEntry const*
{
    for (auto const& entry: builtinCallEntries)
        if (entry.name == name)
            return &entry;
    return nullptr;
}

/// Looks up a pipeline-supporting builtin call entry by name.
/// Returns nullptr if not found or not pipeline-capable.
constexpr auto findPipelineBuiltinEntry(std::string_view name) -> BuiltinCallEntry const*
{
    for (auto const& entry: builtinCallEntries)
        if (entry.name == name && entry.pipelineSupport)
            return &entry;
    return nullptr;
}

} // namespace endo
