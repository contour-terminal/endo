// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/StdlibDescriptors.hpp>

#include <array>

namespace endo
{

// clang-format off

/// Unified table of all stdlib functions.
///
/// Entries with non-empty userFacingName are exposed for completion.
/// Entries with non-null sharedImpl are available via resolveStdlibImpl().
/// Multi-arity overloads use separate entries; only the first carries completion data.
static constexpr std::array descriptors = {
    // -----------------------------------------------------------------------
    // Type Conversion
    // -----------------------------------------------------------------------
    StdlibDescriptor { "string_length", "string_length", 1, nullptr,
        "string_length s -> int",
        "**string_length** `s -> int`\n\nReturns the length of string **s** in characters." },
    StdlibDescriptor { "int_of_string", "int_of_string", 1, nullptr,
        "int_of_string s -> int",
        "**int_of_string** `s -> int`\n\nParses string **s** as an integer." },
    StdlibDescriptor { "string_of_int", "string_of_int", 1, nullptr,
        "string_of_int n -> string",
        "**string_of_int** `n -> string`\n\nConverts integer **n** to its string representation." },
    StdlibDescriptor { "not", "not", 1, nullptr,
        "not b -> bool",
        "**not** `b -> bool`\n\nLogical negation of boolean **b**." },

    // -----------------------------------------------------------------------
    // String Operations
    // -----------------------------------------------------------------------
    StdlibDescriptor { "trim", "string_trim", 1, &builtins::stringTrim,
        "trim s -> string",
        "**trim** `s -> string`\n\nRemoves leading and trailing whitespace from **s**." },
    StdlibDescriptor { "toLower", "string_toLower", 1, &builtins::stringToLower,
        "toLower s -> string",
        "**toLower** `s -> string`\n\nConverts all characters in **s** to lowercase." },
    StdlibDescriptor { "toUpper", "string_toUpper", 1, &builtins::stringToUpper,
        "toUpper s -> string",
        "**toUpper** `s -> string`\n\nConverts all characters in **s** to uppercase." },
    StdlibDescriptor { "contains", "string_contains", 2, &builtins::stringContains,
        "contains substr s -> bool",
        "**contains** `substr s -> bool`\n\nReturns true if **s** contains **substr**." },
    StdlibDescriptor { "startsWith", "string_startsWith", 2, &builtins::stringStartsWith,
        "startsWith prefix s -> bool",
        "**startsWith** `prefix s -> bool`\n\nReturns true if **s** starts with **prefix**." },
    StdlibDescriptor { "endsWith", "string_endsWith", 2, &builtins::stringEndsWith,
        "endsWith suffix s -> bool",
        "**endsWith** `suffix s -> bool`\n\nReturns true if **s** ends with **suffix**." },
    StdlibDescriptor { "replace", "string_replace", 3, &builtins::stringReplace,
        "replace old new s -> string",
        "**replace** `old new s -> string`\n\nReplaces all occurrences of **old** with **new** in **s**." },
    StdlibDescriptor { "split", "string_split", 2, &builtins::stringSplit,
        "split delim s -> list<string>",
        "**split** `delim s -> list<string>`\n\nSplits **s** by delimiter **delim**." },
    StdlibDescriptor { "join", "string_join", 2, &builtins::stringJoin,
        "join delim lst -> string",
        "**join** `delim lst -> string`\n\nJoins list elements with **delim** between them." },

    // -----------------------------------------------------------------------
    // List Basic
    // -----------------------------------------------------------------------
    StdlibDescriptor { "head", "list_head", 1, &builtins::listHead,
        "head lst -> 'a",
        "**head** `lst -> 'a`\n\nReturns the first element of the list." },
    StdlibDescriptor { "tail", "list_tail", 1, &builtins::listTail,
        "tail lst -> list<'a>",
        "**tail** `lst -> list<'a>`\n\nReturns the list without its first element." },
    StdlibDescriptor { "length", "list_length", 1, &builtins::listLength,
        "length lst -> int",
        "**length** `lst -> int`\n\nReturns the number of elements in the list." },
    StdlibDescriptor { "isEmpty", "list_isEmpty", 1, &builtins::listIsEmpty,
        "isEmpty lst -> bool",
        "**isEmpty** `lst -> bool`\n\nReturns true if the list is empty." },
    StdlibDescriptor { "nth", "list_nth", 2, &builtins::listNth,
        "nth n lst -> 'a",
        "**nth** `n lst -> 'a`\n\nReturns the element at index **n** (0-based)." },
    StdlibDescriptor { "last", "list_last", 1, &builtins::listLast,
        "last lst -> 'a",
        "**last** `lst -> 'a`\n\nReturns the last element of the list." },
    StdlibDescriptor { "replicate", "list_replicate", 2, &builtins::listReplicate,
        "replicate n x -> list<'a>",
        "**replicate** `n x -> list<'a>`\n\nCreates a list of **n** copies of **x**." },

    // -----------------------------------------------------------------------
    // List HOFs (IR-generated, no shared impl)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "map", "", 0, nullptr,
        "map f lst -> list<'b>",
        "**map** `f lst -> list<'b>`\n\nApplies function **f** to each element of the list." },
    StdlibDescriptor { "filter", "", 0, nullptr,
        "filter pred lst -> list<'a>",
        "**filter** `pred lst -> list<'a>`\n\nKeeps only elements satisfying **pred**." },
    StdlibDescriptor { "fold", "", 0, nullptr,
        "fold f init lst -> 'b",
        "**fold** `f init lst -> 'b`\n\nReduces the list from the left with **f** and initial value **init**." },
    StdlibDescriptor { "reduce", "", 0, nullptr,
        "reduce f lst -> 'a",
        "**reduce** `f lst -> 'a`\n\nReduces the list from the left with **f** using the first element as initial." },
    StdlibDescriptor { "find", "", 0, nullptr,
        "find pred lst -> option<'a>",
        "**find** `pred lst -> option<'a>`\n\nReturns `Some x` for the first element matching **pred**, or `None`." },
    StdlibDescriptor { "exists", "", 0, nullptr,
        "exists pred lst -> bool",
        "**exists** `pred lst -> bool`\n\nReturns true if any element satisfies **pred**." },
    StdlibDescriptor { "forall", "", 0, nullptr,
        "forall pred lst -> bool",
        "**forall** `pred lst -> bool`\n\nReturns true if all elements satisfy **pred**." },
    StdlibDescriptor { "each", "", 0, nullptr,
        "each f lst -> unit",
        "**each** `f lst -> unit`\n\nApplies **f** to each element for side effects." },

    // -----------------------------------------------------------------------
    // List Transforms (IR-generated HOFs, no shared impl)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "sort", "list_sort", 1, &builtins::listSort,
        "sort lst -> list<'a>",
        "**sort** `lst -> list<'a>`\n\nReturns the list sorted in ascending order." },
    StdlibDescriptor { "reverse", "", 0, nullptr,
        "reverse lst -> list<'a>",
        "**reverse** `lst -> list<'a>`\n\nReturns the list in reverse order." },
    StdlibDescriptor { "distinct", "list_distinct", 1, &builtins::listDistinct,
        "distinct lst -> list<'a>",
        "**distinct** `lst -> list<'a>`\n\nRemoves duplicate elements from the list." },
    StdlibDescriptor { "sortBy", "", 0, nullptr,
        "sortBy f lst -> list<'a>",
        "**sortBy** `f lst -> list<'a>`\n\nSorts the list by the key returned by **f**." },
    StdlibDescriptor { "groupBy", "", 0, nullptr,
        "groupBy f lst -> list<list<'a>>",
        "**groupBy** `f lst -> list<list<'a>>`\n\nGroups consecutive elements with equal keys from **f**." },
    StdlibDescriptor { "take", "", 0, nullptr,
        "take n lst -> list<'a>",
        "**take** `n lst -> list<'a>`\n\nReturns the first **n** elements of the list." },
    StdlibDescriptor { "drop", "", 0, nullptr,
        "drop n lst -> list<'a>",
        "**drop** `n lst -> list<'a>`\n\nSkips the first **n** elements and returns the rest." },
    StdlibDescriptor { "zip", "", 0, nullptr,
        "zip lst1 lst2 -> list<'a * 'b>",
        "**zip** `lst1 lst2 -> list<'a * 'b>`\n\nCombines two lists into a list of pairs." },
    StdlibDescriptor { "flatten", "", 0, nullptr,
        "flatten lst -> list<'a>",
        "**flatten** `lst -> list<'a>`\n\nFlattens a list of lists into a single list." },

    // -----------------------------------------------------------------------
    // Formatting Helpers
    // -----------------------------------------------------------------------
    StdlibDescriptor { "formatNumber", "format_number", 2, &builtins::formatNumber,
        "formatNumber sep n -> string  |  formatNumber n -> string (locale)",
        "**formatNumber** `sep n -> string`\n\nFormats a number with thousands separator **sep**.\nAlso: `formatNumber n` uses locale default." },
    StdlibDescriptor { "formatDateTime", "format_datetime", 1, &builtins::formatDatetime,
        "formatDateTime epoch -> string",
        "**formatDateTime** `epoch -> string`\n\nFormats an epoch timestamp as a human-readable date/time." },
    StdlibDescriptor { "formatMode", "format_mode", 1, &builtins::formatMode,
        "formatMode mode -> string (rwxrwxrwx)",
        "**formatMode** `mode -> string`\n\nFormats a file mode as `rwxrwxrwx` permission string." },
    StdlibDescriptor { "toText", "list_to_string", 1, &builtins::listToString,
        "toText obj -> string",
        "**toText** `obj -> string`\n\nConverts a structured object to a text representation." },
    StdlibDescriptor { "string", "object_to_string", 1, &builtins::objectToString,
        "string x -> string",
        "**string** `x -> string`\n\nConverts any value to its string representation." },

    // -----------------------------------------------------------------------
    // Permission Tests
    // -----------------------------------------------------------------------
    StdlibDescriptor { "isReadable", "mode_isReadable", 1, &builtins::modeIsReadable,
        "isReadable mode -> bool",
        "**isReadable** `mode -> bool`\n\nReturns true if the file mode indicates read permission." },
    StdlibDescriptor { "isWritable", "mode_isWritable", 1, &builtins::modeIsWritable,
        "isWritable mode -> bool",
        "**isWritable** `mode -> bool`\n\nReturns true if the file mode indicates write permission." },
    StdlibDescriptor { "isExecutable", "mode_isExecutable", 1, &builtins::modeIsExecutable,
        "isExecutable mode -> bool",
        "**isExecutable** `mode -> bool`\n\nReturns true if the file mode indicates execute permission." },

    // -----------------------------------------------------------------------
    // Environment/System (user-facing only, Shell provides callbacks)
    // -----------------------------------------------------------------------
    StdlibDescriptor { "env", "env.has", 1, nullptr,
        "env name -> option<string>",
        "**env** `name -> option<string>`\n\nLooks up environment variable **name**. Returns `Some value` or `None`." },
    StdlibDescriptor { "which", "which_find", 1, nullptr,
        "which name -> option<string>",
        "**which** `name -> option<string>`\n\nFinds the full path of command **name** in `$PATH`." },
    StdlibDescriptor { "ps", "structured_ps", 0, nullptr,
        "ps -> list<ProcessInfo>",
        "**ps** `-> list<ProcessInfo>`\n\nReturns a list of running processes with pid, user, cpu, mem, command fields." },
    StdlibDescriptor { "ls", "structured_ls", 1, nullptr,
        "ls -> list<FileInfo>  |  ls path -> list<FileInfo>",
        "**ls** `-> list<FileInfo>`\n\nLists files in the current directory (or given **path**) as structured records." },
    StdlibDescriptor { "rand", "rand", 0, &builtins::randNoArgs,
        "rand -> int  |  rand min max -> int",
        "**rand** `-> int`\n\nReturns a random integer.\nAlso: `rand min max` for a random integer in range." },
    StdlibDescriptor { "fetch", "fetch", 1, nullptr,
        "fetch url -> result<string, string>",
        "**fetch** `url -> result<string, string>`\n\nFetches content from **url**. Returns `Ok body` or `Error msg`." },

    // -----------------------------------------------------------------------
    // Internal-only entries (no user-facing name, only for resolveStdlibImpl)
    // -----------------------------------------------------------------------

    // Multi-arity overloads (completion data already on the primary entry above)
    StdlibDescriptor { "", "format_number", 1, &builtins::formatNumberWithLocale, "", "" },
    StdlibDescriptor { "", "rand", 2, &builtins::randRange, "", "" },

    // Internal list operations
    StdlibDescriptor { "", "list_concat", 2, &builtins::listConcat, "", "" },
    StdlibDescriptor { "", "list_sort_pairs", 1, &builtins::listSortPairs, "", "" },
    StdlibDescriptor { "", "list_group_pairs", 1, &builtins::listGroupPairs, "", "" },
    StdlibDescriptor { "", "list_char_range", 2, &builtins::listCharRange, "", "" },
    StdlibDescriptor { "", "list_range", 3, &builtins::listRange, "", "" },
    StdlibDescriptor { "", "list_replicate", 2, &builtins::listReplicate, "", "" },

    // Internal string operations
    StdlibDescriptor { "", "string_repeat", 2, &builtins::stringRepeat, "", "" },

    // FileMode operations
    StdlibDescriptor { "", "filemode_from_bits", 1, &builtins::fileModeFromBits, "", "" },
    StdlibDescriptor { "", "filemode_is_readable", 1, &builtins::fileModeIsReadable, "", "" },
    StdlibDescriptor { "", "filemode_is_writable", 1, &builtins::fileModeIsWritable, "", "" },
    StdlibDescriptor { "", "filemode_is_executable", 1, &builtins::fileModeIsExecutable, "", "" },
    StdlibDescriptor { "", "filemode_owner", 1, &builtins::fileModeOwner, "", "" },
    StdlibDescriptor { "", "filemode_group", 1, &builtins::fileModeGroup, "", "" },
    StdlibDescriptor { "", "filemode_other", 1, &builtins::fileModeOther, "", "" },

    // Size operations
    StdlibDescriptor { "", "size_from_bytes", 1, &builtins::sizeFromBytes, "", "" },
    StdlibDescriptor { "", "size_from_kb", 1, &builtins::sizeFromKB, "", "" },
    StdlibDescriptor { "", "size_from_mb", 1, &builtins::sizeFromMB, "", "" },
    StdlibDescriptor { "", "size_from_gb", 1, &builtins::sizeFromGB, "", "" },
    StdlibDescriptor { "", "size_from_tb", 1, &builtins::sizeFromTB, "", "" },

    // DateTime operations
    StdlibDescriptor { "", "datetime_now", 0, &builtins::dateTimeNow, "", "" },
    StdlibDescriptor { "", "datetime_from_epoch", 1, &builtins::dateTimeFromEpoch, "", "" },

    // Value-to-string operations
    StdlibDescriptor { "", "object_to_string", 1, &builtins::objectToString, "", "" },
    StdlibDescriptor { "", "list_to_string", 1, &builtins::listToString, "", "" },
};
// clang-format on

std::span<StdlibDescriptor const> stdlibDescriptors()
{
    return descriptors;
}

std::optional<CoreVM::NativeCallback::Functor> resolveStdlibImpl(std::string_view vmName, size_t arity)
{
    for (auto const& desc: descriptors)
        if (desc.sharedImpl && desc.vmName == vmName && desc.arity == arity)
            return CoreVM::NativeCallback::Functor(desc.sharedImpl);
    return std::nullopt;
}

} // namespace endo
