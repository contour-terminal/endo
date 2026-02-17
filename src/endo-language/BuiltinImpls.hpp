// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file BuiltinImpls.hpp
/// @brief Shared stateless builtin callback implementations for the Endo language runtime.
///
/// These functions are the canonical implementations used by Shell, TestHelper, and the WASM
/// playground. Each function has the signature `void(CoreVM::Params&)` and can be bound directly
/// to a NativeCallback registration.

#include <CoreVM/CoreVM.hpp>

#include <optional>
#include <string>

namespace endo::builtins
{

// ---------------------------------------------------------------------------
// Value-to-string conversion (shared across Shell, TestHelper, WASM bridge)
// ---------------------------------------------------------------------------

/// Converts a slot value to string using the known LiteralType from the type tag slot.
/// Strings inside containers are wrapped in double quotes when @p quoteStrings is true.
std::string slotValueToString(uint64_t rawVal,
                              CoreVM::LiteralType type,
                              CoreVM::Runner* runner,
                              bool quoteStrings = true);

/// Recursively converts a runtime value (number, tuple, list, option, record, etc.)
/// to a printable string representation.
std::string valueToString(uint64_t rawVal, CoreVM::Runner* runner);

// ---------------------------------------------------------------------------
// List operations
// ---------------------------------------------------------------------------

/// list_concat(left, right) -> list: Concatenates two lists, preserving element type.
void listConcat(CoreVM::Params& args);

/// list_head(list) -> Option: Returns Some(head) or None.
void listHead(CoreVM::Params& args);

/// list_tail(list) -> list: Returns tail of list (or [] for empty).
void listTail(CoreVM::Params& args);

/// list_length(list) -> int: Returns number of elements.
void listLength(CoreVM::Params& args);

/// list_isEmpty(list) -> bool: Returns true if list is Nil.
void listIsEmpty(CoreVM::Params& args);

/// list_sort(list) -> list: Sorts list elements numerically (ascending).
void listSort(CoreVM::Params& args);

/// list_distinct(list) -> list: Removes duplicates preserving first-seen order.
void listDistinct(CoreVM::Params& args);

/// list_sort_pairs(pairs) -> list: Sorts Tuple2(key, elem) by key, returns elements.
void listSortPairs(CoreVM::Params& args);

/// list_group_pairs(pairs) -> list: Groups Tuple2(key, elem) by key.
/// Returns List<Tuple2<key, List<elem>>>.
void listGroupPairs(CoreVM::Params& args);

/// list_nth(index, list) -> Option: Returns Some(element) or None at given index.
void listNth(CoreVM::Params& args);

/// list_last(list) -> Option: Returns Some(lastElement) or None.
void listLast(CoreVM::Params& args);

/// list_replicate(count, value) -> list: Creates a list of N copies of a value.
void listReplicate(CoreVM::Params& args);

/// list_char_range(startOrd, endOrd) -> list: Builds list of single-char strings from ordinal range.
void listCharRange(CoreVM::Params& args);

/// list_range(start, step, end) -> list: Builds list of numbers from start to end with step.
void listRange(CoreVM::Params& args);

/// list_to_string(obj) -> string: Converts list object to "[1; 2; 3]" string.
void listToString(CoreVM::Params& args);

/// object_to_string(obj) -> string: Runtime dispatch for object printing.
void objectToString(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// String operations
// ---------------------------------------------------------------------------

/// string_repeat(str, count) -> string: Repeats string N times.
void stringRepeat(CoreVM::Params& args);

/// string_replace(old, new, text) -> string: Replaces all occurrences.
void stringReplace(CoreVM::Params& args);

/// string_split(delimiter, text) -> list<string>: Splits text by delimiter.
void stringSplit(CoreVM::Params& args);

/// string_join(separator, list) -> string: Joins list elements with separator.
void stringJoin(CoreVM::Params& args);

/// string_trim(text) -> string: Removes leading/trailing whitespace.
void stringTrim(CoreVM::Params& args);

/// string_toLower(text) -> string: Converts to lowercase.
void stringToLower(CoreVM::Params& args);

/// string_toUpper(text) -> string: Converts to uppercase.
void stringToUpper(CoreVM::Params& args);

/// string_contains(haystack, needle) -> bool: Checks if haystack contains needle.
void stringContains(CoreVM::Params& args);

/// string_startsWith(text, prefix) -> bool: Checks prefix match.
void stringStartsWith(CoreVM::Params& args);

/// string_endsWith(text, suffix) -> bool: Checks suffix match.
void stringEndsWith(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

/// format_datetime(epoch) -> string: Formats epoch timestamp as "YYYY-MM-DD HH:MM:SS".
void formatDatetime(CoreVM::Params& args);

/// format_mode(mode) -> string: Formats Unix file mode as "rwxrwxrwx" string.
void formatMode(CoreVM::Params& args);

/// mode_isReadable(mode) -> bool: Tests if any read bit is set.
void modeIsReadable(CoreVM::Params& args);

/// mode_isWritable(mode) -> bool: Tests if any write bit is set.
void modeIsWritable(CoreVM::Params& args);

/// mode_isExecutable(mode) -> bool: Tests if any execute bit is set.
void modeIsExecutable(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Random number generation
// ---------------------------------------------------------------------------

/// rand() -> int: Returns a random positive integer > 0.
void randNoArgs(CoreVM::Params& args);

/// rand(min, max) -> int: Returns a random integer in [min, max].
void randRange(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Shared implementation resolver
// ---------------------------------------------------------------------------

/// Returns the shared stateless implementation for a given builtin name and arity,
/// or std::nullopt if no shared implementation exists.
std::optional<CoreVM::NativeCallback::Functor> resolveSharedImpl(std::string_view name, size_t arity);

} // namespace endo::builtins
