// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/StdlibDescriptors.hpp>

#include <array>

namespace endo
{

using LT = CoreVM::LiteralType;

// clang-format off

// ---------------------------------------------------------------------------
// Param descriptor arrays (shared where param names/types match)
// ---------------------------------------------------------------------------

// Common single-param patterns
static constexpr ParamDescriptor textStringParam[] = { { "text", LT::String } };
static constexpr ParamDescriptor keyStringParam[] = { { "key", LT::String } };
static constexpr ParamDescriptor listNumberParam[] = { { "list", LT::Number } };
static constexpr ParamDescriptor objNumberParam[] = { { "obj", LT::Number } };
static constexpr ParamDescriptor nNumberParam[] = { { "n", LT::Number } };
static constexpr ParamDescriptor epochNumberParam[] = { { "epoch", LT::Number } };
static constexpr ParamDescriptor modeNumberParam[] = { { "mode", LT::Number } };
static constexpr ParamDescriptor programStringParam[] = { { "program", LT::String } };
static constexpr ParamDescriptor urlStringParam[] = { { "url", LT::String } };
static constexpr ParamDescriptor nameStringParam[] = { { "name", LT::String } };
static constexpr ParamDescriptor valueNumberParam[] = { { "value", LT::Number } };
static constexpr ParamDescriptor pairsNumberParam[] = { { "pairs", LT::Number } };
static constexpr ParamDescriptor mdNumberParam[] = { { "md", LT::Number } };

// Multi-param patterns
static constexpr ParamDescriptor exportTwoParams[] = { { "name", LT::String }, { "value", LT::String } };
static constexpr ParamDescriptor listConcatParams[] = { { "left", LT::Number }, { "right", LT::Number } };
static constexpr ParamDescriptor listNthParams[] = { { "index", LT::Number }, { "list", LT::Number } };
static constexpr ParamDescriptor listReplicateParams[] = { { "count", LT::Number }, { "value", LT::Number } };
static constexpr ParamDescriptor listCharRangeParams[] = { { "start", LT::Number }, { "end", LT::Number } };
static constexpr ParamDescriptor listRangeParams[] = { { "start", LT::Number }, { "step", LT::Number }, { "end", LT::Number } };
static constexpr ParamDescriptor stringRepeatParams[] = { { "str", LT::String }, { "count", LT::Number } };
static constexpr ParamDescriptor stringReplaceParams[] = { { "old_str", LT::String }, { "new_str", LT::String }, { "text", LT::String } };
static constexpr ParamDescriptor stringSplitParams[] = { { "delimiter", LT::String }, { "text", LT::String } };
static constexpr ParamDescriptor stringJoinParams[] = { { "separator", LT::String }, { "list", LT::Number } };
static constexpr ParamDescriptor stringContainsParams[] = { { "substr", LT::String }, { "text", LT::String } };
static constexpr ParamDescriptor stringStartsWithParams[] = { { "prefix", LT::String }, { "text", LT::String } };
static constexpr ParamDescriptor stringEndsWithParams[] = { { "suffix", LT::String }, { "text", LT::String } };
static constexpr ParamDescriptor formatNumberTwoParams[] = { { "separator", LT::String }, { "number", LT::Number } };
static constexpr ParamDescriptor formatNumberOneParams[] = { { "number", LT::Number } };
static constexpr ParamDescriptor randRangeParams[] = { { "min", LT::Number }, { "max", LT::Number } };
static constexpr ParamDescriptor fetchTwoParams[] = { { "url", LT::String }, { "headers", LT::Number } };
static constexpr ParamDescriptor jsonQueryParams[] = { { "path", LT::String }, { "json", LT::String } };

// ---------------------------------------------------------------------------
// Unified descriptor table
// ---------------------------------------------------------------------------

/// Unified table of all stdlib functions.
///
/// Entries with non-empty userFacingName are exposed for completion.
/// Entries with non-empty vmName are registered as native callbacks by registerFSharpBuiltins().
/// Entries with non-null sharedImpl are available via resolveStdlibImpl().
/// Multi-arity overloads use separate entries; only the first carries completion data.
static const std::array descriptors = {
    // -----------------------------------------------------------------------
    // Output (Shell builtins — not stdlib candidates, but need VM registration)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "", "print", LT::Void, textStringParam, nullptr, "", "" },
    StdlibDescriptor { "", "println", LT::Void, textStringParam, nullptr, "", "" },

    // -----------------------------------------------------------------------
    // Type Conversion (IR-generated, no VM registration)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "string_length", "", LT::Void, {}, nullptr,
        "string_length s -> int",
        "**string_length** `s -> int`\n\nReturns the length of string **s** in characters." },
    StdlibDescriptor { "int_of_string", "", LT::Void, {}, nullptr,
        "int_of_string s -> int",
        "**int_of_string** `s -> int`\n\nParses string **s** as an integer." },
    StdlibDescriptor { "string_of_int", "", LT::Void, {}, nullptr,
        "string_of_int n -> string",
        "**string_of_int** `n -> string`\n\nConverts integer **n** to its string representation." },
    StdlibDescriptor { "not", "", LT::Void, {}, nullptr,
        "not b -> bool",
        "**not** `b -> bool`\n\nLogical negation of boolean **b**." },
    StdlibDescriptor { "force", "", LT::Void, {}, nullptr,
        "force lazy<'T> -> 'T",
        "**force** `lazy<'T> -> 'T`\n\nForces evaluation of a lazy value. First call evaluates and caches the result; subsequent calls return the cached value." },

    // -----------------------------------------------------------------------
    // String Operations
    // -----------------------------------------------------------------------
    StdlibDescriptor { "trim", "string_trim", LT::String, textStringParam, &builtins::stringTrim,
        "trim s -> string",
        "**trim** `s -> string`\n\nRemoves leading and trailing whitespace from **s**." },
    StdlibDescriptor { "toLower", "string_toLower", LT::String, textStringParam, &builtins::stringToLower,
        "toLower s -> string",
        "**toLower** `s -> string`\n\nConverts all characters in **s** to lowercase." },
    StdlibDescriptor { "toUpper", "string_toUpper", LT::String, textStringParam, &builtins::stringToUpper,
        "toUpper s -> string",
        "**toUpper** `s -> string`\n\nConverts all characters in **s** to uppercase." },
    StdlibDescriptor { "contains", "string_contains", LT::Boolean, stringContainsParams, &builtins::stringContains,
        "contains substr s -> bool",
        "**contains** `substr s -> bool`\n\nReturns true if **s** contains **substr**." },
    StdlibDescriptor { "startsWith", "string_startsWith", LT::Boolean, stringStartsWithParams, &builtins::stringStartsWith,
        "startsWith prefix s -> bool",
        "**startsWith** `prefix s -> bool`\n\nReturns true if **s** starts with **prefix**." },
    StdlibDescriptor { "endsWith", "string_endsWith", LT::Boolean, stringEndsWithParams, &builtins::stringEndsWith,
        "endsWith suffix s -> bool",
        "**endsWith** `suffix s -> bool`\n\nReturns true if **s** ends with **suffix**." },
    StdlibDescriptor { "replace", "string_replace", LT::String, stringReplaceParams, &builtins::stringReplace,
        "replace old new s -> string",
        "**replace** `old new s -> string`\n\nReplaces all occurrences of **old** with **new** in **s**." },
    StdlibDescriptor { "split", "string_split", LT::Number, stringSplitParams, &builtins::stringSplit,
        "split delim s -> list<string>",
        "**split** `delim s -> list<string>`\n\nSplits **s** by delimiter **delim**." },
    StdlibDescriptor { "join", "string_join", LT::String, stringJoinParams, &builtins::stringJoin,
        "join delim lst -> string",
        "**join** `delim lst -> string`\n\nJoins list elements with **delim** between them." },

    // -----------------------------------------------------------------------
    // List Basic
    // -----------------------------------------------------------------------
    StdlibDescriptor { "head", "list_head", LT::Number, listNumberParam, &builtins::listHead,
        "head lst -> 'a",
        "**head** `lst -> 'a`\n\nReturns the first element of the list." },
    StdlibDescriptor { "tail", "list_tail", LT::Number, listNumberParam, &builtins::listTail,
        "tail lst -> list<'a>",
        "**tail** `lst -> list<'a>`\n\nReturns the list without its first element." },
    StdlibDescriptor { "length", "list_length", LT::Number, listNumberParam, &builtins::listLength,
        "length lst -> int",
        "**length** `lst -> int`\n\nReturns the number of elements in the list." },
    StdlibDescriptor { "isEmpty", "list_isEmpty", LT::Boolean, listNumberParam, &builtins::listIsEmpty,
        "isEmpty lst -> bool",
        "**isEmpty** `lst -> bool`\n\nReturns true if the list is empty." },
    StdlibDescriptor { "nth", "list_nth", LT::Number, listNthParams, &builtins::listNth,
        "nth n lst -> 'a",
        "**nth** `n lst -> 'a`\n\nReturns the element at index **n** (0-based)." },
    StdlibDescriptor { "last", "list_last", LT::Number, listNumberParam, &builtins::listLast,
        "last lst -> 'a",
        "**last** `lst -> 'a`\n\nReturns the last element of the list." },
    StdlibDescriptor { "replicate", "list_replicate", LT::Number, listReplicateParams, &builtins::listReplicate,
        "replicate n x -> list<'a>",
        "**replicate** `n x -> list<'a>`\n\nCreates a list of **n** copies of **x**." },

    // -----------------------------------------------------------------------
    // List HOFs (IR-generated, no VM registration)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "map", "", LT::Void, {}, nullptr,
        "map f lst -> list<'b>",
        "**map** `f lst -> list<'b>`\n\nApplies function **f** to each element of the list." },
    StdlibDescriptor { "filter", "", LT::Void, {}, nullptr,
        "filter pred lst -> list<'a>",
        "**filter** `pred lst -> list<'a>`\n\nKeeps only elements satisfying **pred**." },
    StdlibDescriptor { "fold", "", LT::Void, {}, nullptr,
        "fold f init lst -> 'b",
        "**fold** `f init lst -> 'b`\n\nReduces the list from the left with **f** and initial value **init**." },
    StdlibDescriptor { "reduce", "", LT::Void, {}, nullptr,
        "reduce f lst -> 'a",
        "**reduce** `f lst -> 'a`\n\nReduces the list from the left with **f** using the first element as initial." },
    StdlibDescriptor { "find", "", LT::Void, {}, nullptr,
        "find pred lst -> option<'a>",
        "**find** `pred lst -> option<'a>`\n\nReturns `Some x` for the first element matching **pred**, or `None`." },
    StdlibDescriptor { "exists", "", LT::Void, {}, nullptr,
        "exists pred lst -> bool",
        "**exists** `pred lst -> bool`\n\nReturns true if any element satisfies **pred**." },
    StdlibDescriptor { "forall", "", LT::Void, {}, nullptr,
        "forall pred lst -> bool",
        "**forall** `pred lst -> bool`\n\nReturns true if all elements satisfy **pred**." },
    StdlibDescriptor { "each", "", LT::Void, {}, nullptr,
        "each f lst -> unit",
        "**each** `f lst -> unit`\n\nApplies **f** to each element for side effects." },

    // -----------------------------------------------------------------------
    // List Transforms
    // -----------------------------------------------------------------------
    StdlibDescriptor { "sort", "list_sort", LT::Number, listNumberParam, &builtins::listSort,
        "sort lst -> list<'a>",
        "**sort** `lst -> list<'a>`\n\nReturns the list sorted in ascending order." },
    StdlibDescriptor { "reverse", "", LT::Void, {}, nullptr,
        "reverse lst -> list<'a>",
        "**reverse** `lst -> list<'a>`\n\nReturns the list in reverse order." },
    StdlibDescriptor { "distinct", "list_distinct", LT::Number, listNumberParam, &builtins::listDistinct,
        "distinct lst -> list<'a>",
        "**distinct** `lst -> list<'a>`\n\nRemoves duplicate elements from the list." },
    StdlibDescriptor { "sortBy", "", LT::Void, {}, nullptr,
        "sortBy f lst -> list<'a>",
        "**sortBy** `f lst -> list<'a>`\n\nSorts the list by the key returned by **f**." },
    StdlibDescriptor { "groupBy", "", LT::Void, {}, nullptr,
        "groupBy f lst -> list<list<'a>>",
        "**groupBy** `f lst -> list<list<'a>>`\n\nGroups consecutive elements with equal keys from **f**." },
    StdlibDescriptor { "take", "", LT::Void, {}, nullptr,
        "take n lst -> list<'a>",
        "**take** `n lst -> list<'a>`\n\nReturns the first **n** elements of the list." },
    StdlibDescriptor { "drop", "", LT::Void, {}, nullptr,
        "drop n lst -> list<'a>",
        "**drop** `n lst -> list<'a>`\n\nSkips the first **n** elements and returns the rest." },
    StdlibDescriptor { "zip", "", LT::Void, {}, nullptr,
        "zip lst1 lst2 -> list<'a * 'b>",
        "**zip** `lst1 lst2 -> list<'a * 'b>`\n\nCombines two lists into a list of pairs." },
    StdlibDescriptor { "flatten", "", LT::Void, {}, nullptr,
        "flatten lst -> list<'a>",
        "**flatten** `lst -> list<'a>`\n\nFlattens a list of lists into a single list." },

    // -----------------------------------------------------------------------
    // Formatting Helpers
    // -----------------------------------------------------------------------
    StdlibDescriptor { "formatNumber", "format_number", LT::String, formatNumberTwoParams, &builtins::formatNumber,
        "formatNumber sep n -> string  |  formatNumber n -> string (locale)",
        "**formatNumber** `sep n -> string`\n\nFormats a number with thousands separator **sep**.\nAlso: `formatNumber n` uses locale default." },
    StdlibDescriptor { "formatDateTime", "format_datetime", LT::String, epochNumberParam, &builtins::formatDatetime,
        "formatDateTime epoch -> string",
        "**formatDateTime** `epoch -> string`\n\nFormats an epoch timestamp as a human-readable date/time." },
    StdlibDescriptor { "formatMode", "format_mode", LT::String, modeNumberParam, &builtins::formatMode,
        "formatMode mode -> string (rwxrwxrwx)",
        "**formatMode** `mode -> string`\n\nFormats a file mode as `rwxrwxrwx` permission string." },
    StdlibDescriptor { "toText", "list_to_string", LT::String, objNumberParam, &builtins::listToString,
        "toText obj -> string",
        "**toText** `obj -> string`\n\nConverts a structured object to a text representation." },
    StdlibDescriptor { "string", "object_to_string", LT::String, objNumberParam, &builtins::objectToString,
        "string x -> string",
        "**string** `x -> string`\n\nConverts any value to its string representation." },

    // -----------------------------------------------------------------------
    // Permission Tests
    // -----------------------------------------------------------------------
    StdlibDescriptor { "isReadable", "mode_isReadable", LT::Boolean, modeNumberParam, &builtins::modeIsReadable,
        "isReadable mode -> bool",
        "**isReadable** `mode -> bool`\n\nReturns true if the file mode indicates read permission." },
    StdlibDescriptor { "isWritable", "mode_isWritable", LT::Boolean, modeNumberParam, &builtins::modeIsWritable,
        "isWritable mode -> bool",
        "**isWritable** `mode -> bool`\n\nReturns true if the file mode indicates write permission." },
    StdlibDescriptor { "isExecutable", "mode_isExecutable", LT::Boolean, modeNumberParam, &builtins::modeIsExecutable,
        "isExecutable mode -> bool",
        "**isExecutable** `mode -> bool`\n\nReturns true if the file mode indicates execute permission." },

    // -----------------------------------------------------------------------
    // Environment/System (user-facing, Shell provides callbacks)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "env", "env.has", LT::Boolean, keyStringParam, nullptr,
        "env name -> option<string>",
        "**env** `name -> option<string>`\n\nLooks up environment variable **name**. Returns `Some value` or `None`." },
    StdlibDescriptor { "which", "which_find", LT::Number, programStringParam, nullptr,
        "which name -> option<string>",
        "**which** `name -> option<string>`\n\nFinds the full path of command **name** in `$PATH`." },
    StdlibDescriptor { "ps", "", LT::Void, {}, nullptr,
        "ps -> list<ProcessInfo>",
        "**ps** `-> list<ProcessInfo>`\n\nReturns a list of running processes with pid, user, cpu, mem, command fields." },
    StdlibDescriptor { "ls", "", LT::Void, {}, nullptr,
        "ls -> list<FileInfo>  |  ls path -> list<FileInfo>",
        "**ls** `-> list<FileInfo>`\n\nLists files in the current directory (or given **path**) as structured records." },
    StdlibDescriptor { "rand", "rand", LT::Number, {}, &builtins::randNoArgs,
        "rand -> int  |  rand min max -> int",
        "**rand** `-> int`\n\nReturns a random integer.\nAlso: `rand min max` for a random integer in range." },
    StdlibDescriptor { "fetch", "fetch", LT::Number, urlStringParam, nullptr,
        "fetch url -> result<string, string>",
        "**fetch** `url -> result<string, string>`\n\nFetches content from **url**. Returns `Ok body` or `Error msg`." },

    // -----------------------------------------------------------------------
    // Internal-only entries (no user-facing name)
    // -----------------------------------------------------------------------

    // env.get — separate VM registration
    StdlibDescriptor { "", "env.get", LT::String, keyStringParam, nullptr, "", "" },

    // export — F#-style overloads
    StdlibDescriptor { "", "export", LT::Void, exportTwoParams, nullptr, "", "" },
    StdlibDescriptor { "", "export", LT::Void, nameStringParam, nullptr, "", "" },

    // display_result — Shell/REPL only
    StdlibDescriptor { "", "display_result", LT::Void, valueNumberParam, nullptr, "", "" },

    // Multi-arity overloads
    StdlibDescriptor { "", "format_number", LT::String, formatNumberOneParams, &builtins::formatNumberWithLocale, "", "" },
    StdlibDescriptor { "", "rand", LT::Number, randRangeParams, &builtins::randRange, "", "" },
    StdlibDescriptor { "", "fetch", LT::Number, fetchTwoParams, nullptr, "", "" },

    // Internal list operations
    StdlibDescriptor { "", "list_concat", LT::Number, listConcatParams, &builtins::listConcat, "", "" },
    StdlibDescriptor { "", "list_sort_pairs", LT::Number, pairsNumberParam, &builtins::listSortPairs, "", "" },
    StdlibDescriptor { "", "list_group_pairs", LT::Number, pairsNumberParam, &builtins::listGroupPairs, "", "" },
    StdlibDescriptor { "", "list_char_range", LT::Number, listCharRangeParams, &builtins::listCharRange, "", "" },
    StdlibDescriptor { "", "list_range", LT::Number, listRangeParams, &builtins::listRange, "", "" },

    // Internal string operations
    StdlibDescriptor { "", "string_repeat", LT::String, stringRepeatParams, &builtins::stringRepeat, "", "" },

    // FileMode operations
    StdlibDescriptor { "", "filemode_from_bits", LT::Number, nNumberParam, &builtins::fileModeFromBits, "", "" },
    StdlibDescriptor { "", "filemode_is_readable", LT::Boolean, objNumberParam, &builtins::fileModeIsReadable, "", "" },
    StdlibDescriptor { "", "filemode_is_writable", LT::Boolean, objNumberParam, &builtins::fileModeIsWritable, "", "" },
    StdlibDescriptor { "", "filemode_is_executable", LT::Boolean, objNumberParam, &builtins::fileModeIsExecutable, "", "" },
    StdlibDescriptor { "", "filemode_owner", LT::Number, objNumberParam, &builtins::fileModeOwner, "", "" },
    StdlibDescriptor { "", "filemode_group", LT::Number, objNumberParam, &builtins::fileModeGroup, "", "" },
    StdlibDescriptor { "", "filemode_other", LT::Number, objNumberParam, &builtins::fileModeOther, "", "" },

    // TimeSpan operations
    StdlibDescriptor { "", "timespan_from_ms", LT::Number, nNumberParam, &builtins::timespanFromMs, "", "" },
    StdlibDescriptor { "", "timespan_from_seconds", LT::Number, nNumberParam, &builtins::timespanFromSeconds, "", "" },
    StdlibDescriptor { "", "timespan_from_minutes", LT::Number, nNumberParam, &builtins::timespanFromMinutes, "", "" },
    StdlibDescriptor { "", "timespan_from_hours", LT::Number, nNumberParam, &builtins::timespanFromHours, "", "" },
    StdlibDescriptor { "", "timespan_from_days", LT::Number, nNumberParam, &builtins::timespanFromDays, "", "" },
    StdlibDescriptor { "sleep", "timespan_sleep", LT::Number, objNumberParam, &builtins::timespanSleep,
        "sleep ts -> unit",
        "**sleep** `TimeSpan -> unit`\n\nPauses execution for the given TimeSpan duration." },
    StdlibDescriptor { "formatTimeSpan", "format_timespan", LT::String, objNumberParam, &builtins::formatTimeSpan,
        "formatTimeSpan ts -> string",
        "**formatTimeSpan** `TimeSpan -> string`\n\nFormats a TimeSpan as a human-readable duration string." },

    // Size operations
    StdlibDescriptor { "", "size_from_bytes", LT::Number, nNumberParam, &builtins::sizeFromBytes, "", "" },
    StdlibDescriptor { "", "size_from_kb", LT::Number, nNumberParam, &builtins::sizeFromKB, "", "" },
    StdlibDescriptor { "", "size_from_mb", LT::Number, nNumberParam, &builtins::sizeFromMB, "", "" },
    StdlibDescriptor { "", "size_from_gb", LT::Number, nNumberParam, &builtins::sizeFromGB, "", "" },
    StdlibDescriptor { "", "size_from_tb", LT::Number, nNumberParam, &builtins::sizeFromTB, "", "" },

    // Timing
    StdlibDescriptor { "", "__monotonic_ms", LT::Number, {}, &builtins::monotonicMs, "", "" },
    StdlibDescriptor { "time", "", LT::Void, {}, nullptr,
        "time { body } -> TimeSpan",
        "**time** `{ body } -> TimeSpan`\n\n"
        "Measures the execution time of a computation expression and returns a TimeSpan.\n\n"
        "```endo\ntime { sleep (TimeSpan.fromSeconds 1) }\n```" },

    // DateTime operations
    StdlibDescriptor { "", "datetime_now", LT::Number, {}, &builtins::dateTimeNow, "", "" },
    StdlibDescriptor { "", "datetime_from_epoch", LT::Number, epochNumberParam, &builtins::dateTimeFromEpoch, "", "" },

    // Markdown operations
    StdlibDescriptor { "markdown", "markdown_create", LT::Number, textStringParam, &builtins::markdownCreate,
        "markdown text -> Markdown",
        "**markdown** `text -> Markdown`\n\nCreates a Markdown object from a string." },
    StdlibDescriptor { "", "markdown_to_html", LT::String, mdNumberParam, &builtins::markdownToHtml, "", "" },
    StdlibDescriptor { "", "markdown_to_text", LT::String, mdNumberParam, &builtins::markdownToText, "", "" },
    StdlibDescriptor { "", "markdown_content", LT::String, mdNumberParam, &builtins::markdownContent, "", "" },
    StdlibDescriptor { "", "markdown_render", LT::Void, mdNumberParam, nullptr, "", "" },

    // Json operations
    StdlibDescriptor { "", "json_query", LT::Number, jsonQueryParams, &builtins::jsonQuery,
        "Json.query path json -> list<string>",
        "**Json.query** `path json -> list<string>`\n\n"
        "Extracts values from a JSON string using a dotted path.\n\n"
        "Path syntax: `.key` accesses an object property, `[]` iterates array elements.\n"
        "Example: `Json.query \".presets[].name\" json_str`" },
};
// clang-format on

std::span<StdlibDescriptor const> stdlibDescriptors()
{
    return descriptors;
}

std::optional<CoreVM::NativeCallback::Functor> resolveStdlibImpl(std::string_view vmName, size_t arity)
{
    for (auto const& desc: descriptors)
        if (desc.sharedImpl && desc.vmName == vmName && desc.params.size() == arity)
            return CoreVM::NativeCallback::Functor(desc.sharedImpl);
    return std::nullopt;
}

} // namespace endo
