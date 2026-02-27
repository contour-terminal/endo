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

/// format_number(separator, number) -> string: Formats integer with thousand separators.
/// E.g. formatNumber "," 1234567 -> "1,234,567", formatNumber "." 1234567 -> "1.234.567".
void formatNumber(CoreVM::Params& args);

/// format_number(number) -> string: Formats integer using the user's locale thousand separator.
void formatNumberWithLocale(CoreVM::Params& args);

/// mode_isReadable(mode) -> bool: Tests if any read bit is set.
void modeIsReadable(CoreVM::Params& args);

/// mode_isWritable(mode) -> bool: Tests if any write bit is set.
void modeIsWritable(CoreVM::Params& args);

/// mode_isExecutable(mode) -> bool: Tests if any execute bit is set.
void modeIsExecutable(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// FileMode operations
// ---------------------------------------------------------------------------

/// Formats raw Unix permission bits as a "rwxrwxrwx" permission string.
/// @param mode Raw Unix permission bits
/// @return Permission string (e.g., "rwxr-xr-x")
std::string formatFileModeToString(int64_t mode);

/// Creates a FileMode record object from raw Unix permission bits.
/// @param runner The runner instance for object allocation
/// @param mode Raw Unix permission bits
/// @return Pointer to the newly allocated FileMode TypedObject
CoreVM::TypedObject* makeFileModeFromBits(CoreVM::Runner* runner, int64_t mode);

/// filemode_from_bits(n) -> FileMode: Creates a FileMode from raw permission bits.
void fileModeFromBits(CoreVM::Params& args);

/// filemode_is_readable(obj) -> bool: Tests if any read bit is set.
void fileModeIsReadable(CoreVM::Params& args);

/// filemode_is_writable(obj) -> bool: Tests if any write bit is set.
void fileModeIsWritable(CoreVM::Params& args);

/// filemode_is_executable(obj) -> bool: Tests if any execute bit is set.
void fileModeIsExecutable(CoreVM::Params& args);

/// filemode_owner(obj) -> int: Returns the owner permission digit (0-7).
void fileModeOwner(CoreVM::Params& args);

/// filemode_group(obj) -> int: Returns the group permission digit (0-7).
void fileModeGroup(CoreVM::Params& args);

/// filemode_other(obj) -> int: Returns the other permission digit (0-7).
void fileModeOther(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Size operations
// ---------------------------------------------------------------------------

/// Formats a raw byte count into a human-readable string (e.g., "42 B", "1.5 KB", "3 GB").
/// @param bytes Raw byte count
/// @return Formatted size string with appropriate unit suffix
std::string formatSizeToString(int64_t bytes);

/// Creates a Size record object from a raw byte count.
/// @param runner The runner instance for object allocation
/// @param bytes Raw byte count
/// @return Pointer to the newly allocated Size TypedObject
CoreVM::TypedObject* makeSizeFromBytes(CoreVM::Runner* runner, int64_t bytes);

/// size_from_bytes(n) -> Size: Creates a Size from raw bytes.
void sizeFromBytes(CoreVM::Params& args);

/// size_from_kb(n) -> Size: Creates a Size from kilobytes (n * 1024).
void sizeFromKB(CoreVM::Params& args);

/// size_from_mb(n) -> Size: Creates a Size from megabytes (n * 1024^2).
void sizeFromMB(CoreVM::Params& args);

/// size_from_gb(n) -> Size: Creates a Size from gigabytes (n * 1024^3).
void sizeFromGB(CoreVM::Params& args);

/// size_from_tb(n) -> Size: Creates a Size from terabytes (n * 1024^4).
void sizeFromTB(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// TimeSpan operations
// ---------------------------------------------------------------------------

/// Formats a duration in milliseconds as a human-readable string (e.g., "1d 2h 30m 15s 100ms").
/// @param milliseconds Duration in milliseconds
/// @return Formatted duration string
std::string formatTimeSpanToString(int64_t milliseconds);

/// Creates a TimeSpan record object from a millisecond count.
/// @param runner The runner instance for object allocation
/// @param ms Duration in milliseconds
/// @return Pointer to the newly allocated TimeSpan TypedObject
CoreVM::TypedObject* makeTimeSpanFromMs(CoreVM::Runner* runner, int64_t ms);

/// timespan_from_ms(n) -> TimeSpan: Creates a TimeSpan from milliseconds.
void timespanFromMs(CoreVM::Params& args);

/// timespan_from_seconds(n) -> TimeSpan: Creates a TimeSpan from seconds (n * 1000).
void timespanFromSeconds(CoreVM::Params& args);

/// timespan_from_minutes(n) -> TimeSpan: Creates a TimeSpan from minutes (n * 60000).
void timespanFromMinutes(CoreVM::Params& args);

/// timespan_from_hours(n) -> TimeSpan: Creates a TimeSpan from hours (n * 3600000).
void timespanFromHours(CoreVM::Params& args);

/// timespan_from_days(n) -> TimeSpan: Creates a TimeSpan from days (n * 86400000).
void timespanFromDays(CoreVM::Params& args);

/// timespan_sleep(obj) -> unit: Sleeps for the duration specified by a TimeSpan object.
void timespanSleep(CoreVM::Params& args);

/// format_timespan(obj) -> string: Formats a TimeSpan as a human-readable duration string.
void formatTimeSpan(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// DateTime operations
// ---------------------------------------------------------------------------

/// Creates a DateTime record object from a Unix epoch timestamp.
/// @param runner The runner instance for object allocation
/// @param epoch Unix epoch timestamp (seconds since 1970-01-01 UTC)
/// @return Pointer to the newly allocated DateTime TypedObject
CoreVM::TypedObject* makeDateTimeFromEpoch(CoreVM::Runner* runner, int64_t epoch);

/// datetime_now() -> number: Returns a DateTime record with the current UTC time.
void dateTimeNow(CoreVM::Params& args);

/// datetime_from_epoch(epoch: number) -> number: Converts epoch to a DateTime record.
void dateTimeFromEpoch(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Markdown operations
// ---------------------------------------------------------------------------

/// Creates a Markdown record object from a raw markdown content string.
/// @param runner The runner instance for object allocation
/// @param content Raw markdown content string
/// @return Pointer to the newly allocated Markdown TypedObject
CoreVM::TypedObject* makeMarkdown(CoreVM::Runner* runner, std::string const& content);

/// markdown_create(text) -> Markdown: Creates a Markdown object from a string.
void markdownCreate(CoreVM::Params& args);

/// markdown_to_html(md) -> string: Converts markdown to basic HTML.
void markdownToHtml(CoreVM::Params& args);

/// markdown_to_text(md) -> string: Strips markdown formatting, returns plain text.
void markdownToText(CoreVM::Params& args);

/// markdown_content(md) -> string: Extracts the raw content string from a Markdown object.
void markdownContent(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Random number generation
// ---------------------------------------------------------------------------

/// __monotonic_ms() -> int: Returns current monotonic clock time in milliseconds.
void monotonicMs(CoreVM::Params& args);

/// rand() -> int: Returns a random positive integer > 0.
void randNoArgs(CoreVM::Params& args);

/// rand(min, max) -> int: Returns a random integer in [min, max].
void randRange(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// JSON operations
// ---------------------------------------------------------------------------

/// json_query(path, json) -> list<string>: Extracts values from a JSON string using a dotted path.
/// Path syntax: `.key` accesses an object property, `[]` iterates array elements.
/// Returns an empty list on parse error or missing key.
void jsonQuery(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// File I/O operations
// ---------------------------------------------------------------------------

/// file_open(path, mode) -> result<FileHandle, str>: Opens a file with the given mode.
void fileOpen(CoreVM::Params& args);

/// file_close(fd) -> unit: Closes a file handle.
void fileClose(CoreVM::Params& args);

/// file_read_line(fd) -> option<str>: Reads one line from the file (None at EOF).
void fileReadLine(CoreVM::Params& args);

/// file_read_all(path) -> result<str, str>: Reads the entire file as a string.
void fileReadAll(CoreVM::Params& args);

/// file_write_all(path, content) -> result<unit, str>: Writes a string to a file.
void fileWriteAll(CoreVM::Params& args);

/// file_append_all(path, content) -> result<unit, str>: Appends a string to a file.
void fileAppendAll(CoreVM::Params& args);

/// file_size(path) -> result<int, str>: Returns file size in bytes.
void fileSize(CoreVM::Params& args);

/// file_exists(path) -> bool: Checks if a file exists.
void fileExists(CoreVM::Params& args);

/// file_delete(path) -> result<unit, str>: Deletes a file.
void fileDelete(CoreVM::Params& args);

// ---------------------------------------------------------------------------
// Shared implementation resolver
// ---------------------------------------------------------------------------

/// Returns the shared stateless implementation for a given builtin name and arity,
/// or std::nullopt if no shared implementation exists.
std::optional<CoreVM::NativeCallback::Functor> resolveSharedImpl(std::string_view name, size_t arity);

} // namespace endo::builtins
