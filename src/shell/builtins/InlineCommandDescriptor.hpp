// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file InlineCommandDescriptor.hpp
/// @brief Single source of truth for inline builtin command metadata.
///
/// Each inline builtin is described by one InlineCommandDescriptor entry.
/// This drives: dispatch, arg parsing, help generation, completion specs,
/// and LSP builtin descriptors. Adding a new builtin = one table entry
/// + one implementation function.

#include <shell/completion/CommandSpec.hpp>

#include <endo-language/builtins/BuiltinSignatures.hpp>

#include <CoreVM/CoreVM.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <platform/Types.hpp>

namespace endo
{

// On MSVC/clang-cl, member-function-pointer size depends on the class's inheritance
// model. A bare forward declaration defaults to "unknown inheritance" (24 bytes),
// whereas a fully-defined single-inheritance class yields 8 bytes. If Shell.hpp
// happens to be included before this header, the struct layout changes and causes
// an ODR violation. The __single_inheritance annotation pins the pointer size to
// the correct (single-inheritance) representation regardless of include order.
#if defined(_MSC_VER)
class __single_inheritance Shell;
#else
class Shell;
#endif

/// @brief Describes a flag/option for an inline builtin command.
struct InlineOptionDef
{
    std::string_view shortFlag;   ///< e.g., "-r" (empty if none)
    std::string_view longFlag;    ///< e.g., "--recursive" (empty if none)
    std::string_view description; ///< Help text for this option
    bool takesValue = false;      ///< Whether this flag consumes the next arg as its value
};

/// @brief Dynamic-query completion for an inline builtin's positional arguments.
///
/// When queryTag is non-empty, the generated completion spec resolves
/// positional-argument candidates through a CommandQueryProvider at completion
/// time (e.g. live process names) instead of file paths.
struct InlinePositionalQuery
{
    std::string_view queryTag;         ///< e.g., "process-names" (empty: no dynamic query)
    std::string_view description;      ///< Help text for the positional argument
    bool repeatable = false;           ///< Whether the positional argument repeats
    std::string_view overrideFlag;     ///< Option that switches the query tag (e.g., "-f")
    std::string_view overrideQueryTag; ///< Query tag used while overrideFlag is present
};

/// @brief Single source of truth for an inline builtin command.
///
/// Drives: dispatch, arg parsing, help generation, completion specs,
/// and LSP builtin descriptors.
struct InlineCommandDescriptor
{
    using NoStdinFn = int (Shell::*)(CoreVM::CoreStringArray const&, NativeHandle);
    using WithStdinFn = int (Shell::*)(CoreVM::CoreStringArray const&, NativeHandle, NativeHandle);

    std::string_view name;                      ///< Command name (e.g., "head")
    std::string_view briefDescription;          ///< One-line (e.g., "Output first lines of files")
    std::string_view usageLine;                 ///< e.g., "head [OPTIONS] [FILE...]"
    std::span<InlineOptionDef const> options;   ///< Flag/option definitions
    bool acceptsFileArgs = false;               ///< Whether positional args are file paths
    bool fileArgsRepeatable = false;            ///< Whether multiple file args are accepted
    InlinePositionalQuery positionalQuery = {}; ///< Dynamic-query completion for positional args
    NoStdinFn noStdinFn = nullptr;              ///< Implementation (no stdin)
    WithStdinFn withStdinFn = nullptr;          ///< Implementation (with stdin)

    /// @brief Calls the appropriate function variant.
    [[nodiscard]] int execute(Shell& shell,
                              CoreVM::CoreStringArray const& args,
                              NativeHandle outputFd,
                              NativeHandle stdinFd) const;
    // Defined out-of-line in InlineCommandDescriptors.cpp
};

/// @brief Generates markdown help text from a command descriptor.
[[nodiscard]] std::string generateInlineHelp(InlineCommandDescriptor const& desc);

/// @brief Generates the completion CommandSpec for a single command descriptor.
///
/// Shared by generateBuiltinCompletionSpecs() and by commands that carry a descriptor
/// without being in the dispatch table (see whichDescriptor()).
///
/// @param desc The command descriptor to convert.
/// @return The completion spec: options (plus an implicit -h/--help) and positional args.
[[nodiscard]] CommandSpec specFromInlineDescriptor(InlineCommandDescriptor const& desc);

/// @brief Generates CommandSpec entries for completion from inline command descriptors.
[[nodiscard]] std::vector<CommandSpec> generateBuiltinCompletionSpecs(
    std::span<InlineCommandDescriptor const> descriptors);

/// @brief Generates BuiltinInfo entries for LSP/completion from inline command descriptors.
[[nodiscard]] std::vector<BuiltinInfo> inlineBuiltinInfos(
    std::span<InlineCommandDescriptor const> descriptors);

} // namespace endo
