// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file InlineCommandDescriptor.hpp
/// @brief Single source of truth for inline builtin command metadata.
///
/// Each inline builtin is described by one InlineCommandDescriptor entry.
/// This drives: dispatch, arg parsing, help generation, completion specs,
/// and LSP builtin descriptors. Adding a new builtin = one table entry
/// + one implementation function.

#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <shell/completion/CommandSpec.hpp>

#include <CoreVM/CoreVM.hpp>

#include <platform/Types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

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

/// @brief Single source of truth for an inline builtin command.
///
/// Drives: dispatch, arg parsing, help generation, completion specs,
/// and LSP builtin descriptors.
struct InlineCommandDescriptor
{
    using NoStdinFn = int (Shell::*)(CoreVM::CoreStringArray const&, NativeHandle);
    using WithStdinFn = int (Shell::*)(CoreVM::CoreStringArray const&, NativeHandle, NativeHandle);

    std::string_view name;                       ///< Command name (e.g., "head")
    std::string_view briefDescription;           ///< One-line (e.g., "Output first lines of files")
    std::string_view usageLine;                  ///< e.g., "head [OPTIONS] [FILE...]"
    std::span<InlineOptionDef const> options;     ///< Flag/option definitions
    bool acceptsFileArgs = false;                ///< Whether positional args are file paths
    bool fileArgsRepeatable = false;             ///< Whether multiple file args are accepted
    NoStdinFn noStdinFn = nullptr;               ///< Implementation (no stdin)
    WithStdinFn withStdinFn = nullptr;           ///< Implementation (with stdin)

    /// @brief Calls the appropriate function variant.
    [[nodiscard]] int execute(Shell& shell,
                              CoreVM::CoreStringArray const& args,
                              NativeHandle outputFd,
                              NativeHandle stdinFd) const;
    // Defined out-of-line in InlineCommandDescriptors.cpp
};

/// @brief Generates markdown help text from a command descriptor.
[[nodiscard]] std::string generateInlineHelp(InlineCommandDescriptor const& desc);

/// @brief Generates CommandSpec entries for completion from inline command descriptors.
[[nodiscard]] std::vector<CommandSpec> generateBuiltinCompletionSpecs(
    std::span<InlineCommandDescriptor const> descriptors);

/// @brief Generates BuiltinInfo entries for LSP/completion from inline command descriptors.
[[nodiscard]] std::vector<BuiltinInfo> inlineBuiltinInfos(
    std::span<InlineCommandDescriptor const> descriptors);

} // namespace endo
