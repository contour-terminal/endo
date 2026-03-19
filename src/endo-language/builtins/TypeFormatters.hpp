// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file TypeFormatters.hpp
/// @brief Type-specific formatters for human-readable display of TypedObject values.
///
/// Each builtin type has a dedicated formatter registered via registerBuiltinFormatters().
/// The generic formatProduct/formatSum serve as defaults for user-defined types.

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypeRegistry.hpp>

#include <string>

namespace CoreVM
{
struct TypedObject;
class Runner;
} // namespace CoreVM

namespace endo::builtins
{

// ---------------------------------------------------------------------------
// Individual type formatters (signature matches CoreVM::TypeFormatFn)
// ---------------------------------------------------------------------------

/// Formats a List as "[elem1; elem2; ...]" using element type tags from slot 2.
std::string formatList(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Tuple2 as "(fst, snd)" using packed type tags from slot 2.
std::string formatTuple2(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Tuple3 as "(e0, e1, e2)" using packed type tags from slot 3.
std::string formatTuple3(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats an Option as "Some value" or "None".
std::string formatOption(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Result as "Ok value" or "Error value".
std::string formatResult(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Seq as "seq {}" or "seq { ... }".
std::string formatSeq(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Lazy value as "lazy <unevaluated>".
std::string formatLazy(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a FileHandle as "FileHandle(N)".
std::string formatFileHandle(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Size using formatSizeToString().
std::string formatSize(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a TimeSpan using formatTimeSpanToString().
std::string formatTimeSpan(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a FileMode using formatFileModeToString().
std::string formatFileMode(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Markdown object by extracting content from slot 0.
std::string formatMarkdown(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a DateTime as "YYYY-MM-DD HH:MM:SS".
std::string formatDateTime(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Formats a Ref cell as "ref <value>".
std::string formatRef(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

// ---------------------------------------------------------------------------
// Generic formatters (for user-defined and fallback types)
// ---------------------------------------------------------------------------

/// Generic Product type formatter: "{ field = value; ... }".
std::string formatProduct(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

/// Generic Sum type formatter: "VariantName value" using field types from VariantInfo.
std::string formatSum(CoreVM::TypedObject const& obj, CoreVM::Runner* runner);

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

/// Registers formatFn on all builtin TypeDescriptors.
/// Must be called after TypeRegistry::registerBuiltins().
/// @param registry The TypeRegistry whose builtin types will be updated.
void registerBuiltinFormatters(CoreVM::TypeRegistry& registry);

} // namespace endo::builtins
