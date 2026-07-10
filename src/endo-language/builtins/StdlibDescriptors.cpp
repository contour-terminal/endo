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
static constexpr ParamDescriptor TextStringParam[] = { { .name="text", .type=LT::String } };
static constexpr ParamDescriptor KeyStringParam[] = { { .name="key", .type=LT::String } };
static constexpr ParamDescriptor ListNumberParam[] = { { .name="list", .type=LT::Number } };
static constexpr ParamDescriptor ObjNumberParam[] = { { .name="obj", .type=LT::Number } };
static constexpr ParamDescriptor NNumberParam[] = { { .name="n", .type=LT::Number } };
static constexpr ParamDescriptor EpochNumberParam[] = { { .name="epoch", .type=LT::Number } };
static constexpr ParamDescriptor ModeNumberParam[] = { { .name="mode", .type=LT::Number } };
static constexpr ParamDescriptor ProgramStringParam[] = { { .name="program", .type=LT::String } };
static constexpr ParamDescriptor UrlStringParam[] = { { .name="url", .type=LT::String } };
static constexpr ParamDescriptor NameStringParam[] = { { .name="name", .type=LT::String } };
static constexpr ParamDescriptor ValueNumberParam[] = { { .name="value", .type=LT::Number } };
static constexpr ParamDescriptor PairsNumberParam[] = { { .name="pairs", .type=LT::Number } };
static constexpr ParamDescriptor MdNumberParam[] = { { .name="md", .type=LT::Number } };

// Multi-param patterns
static constexpr ParamDescriptor ExportTwoParams[] = { { .name="name", .type=LT::String }, { .name="value", .type=LT::String } };
static constexpr ParamDescriptor ListConcatParams[] = { { .name="left", .type=LT::Number }, { .name="right", .type=LT::Number } };
static constexpr ParamDescriptor ListNthParams[] = { { .name="index", .type=LT::Number }, { .name="list", .type=LT::Number } };
static constexpr ParamDescriptor ListReplicateParams[] = { { .name="count", .type=LT::Number }, { .name="value", .type=LT::Number } };
static constexpr ParamDescriptor ListCharRangeParams[] = { { .name="start", .type=LT::Number }, { .name="end", .type=LT::Number } };
static constexpr ParamDescriptor ListRangeParams[] = { { .name="start", .type=LT::Number }, { .name="step", .type=LT::Number }, { .name="end", .type=LT::Number } };
static constexpr ParamDescriptor StringRepeatParams[] = { { .name="str", .type=LT::String }, { .name="count", .type=LT::Number } };
static constexpr ParamDescriptor StringReplaceParams[] = { { .name="old_str", .type=LT::String }, { .name="new_str", .type=LT::String }, { .name="text", .type=LT::String } };
static constexpr ParamDescriptor StringSplitParams[] = { { .name="delimiter", .type=LT::String }, { .name="text", .type=LT::String } };
static constexpr ParamDescriptor StringLinesParam[] = { { .name="text", .type=LT::String } };
static constexpr ParamDescriptor StringJoinParams[] = { { .name="separator", .type=LT::String }, { .name="list", .type=LT::Number } };
static constexpr ParamDescriptor StringContainsParams[] = { { .name="substr", .type=LT::String }, { .name="text", .type=LT::String } };
static constexpr ParamDescriptor StringStartsWithParams[] = { { .name="prefix", .type=LT::String }, { .name="text", .type=LT::String } };
static constexpr ParamDescriptor StringEndsWithParams[] = { { .name="suffix", .type=LT::String }, { .name="text", .type=LT::String } };
static constexpr ParamDescriptor FormatNumberTwoParams[] = { { .name="separator", .type=LT::String }, { .name="number", .type=LT::Number } };
static constexpr ParamDescriptor FormatNumberOneParams[] = { { .name="number", .type=LT::Number } };
static constexpr ParamDescriptor RandRangeParams[] = { { .name="min", .type=LT::Number }, { .name="max", .type=LT::Number } };
static constexpr ParamDescriptor FetchTwoParams[] = { { .name="url", .type=LT::String }, { .name="headers", .type=LT::Number } };
static constexpr ParamDescriptor HttpServeParams[] = { { .name="port", .type=LT::Number }, { .name="handler", .type=LT::Function } };
static constexpr ParamDescriptor JsonQueryParams[] = { { .name="path", .type=LT::String }, { .name="json", .type=LT::String } };
static constexpr ParamDescriptor RegisterCompleterParams[] = { { .name="command", .type=LT::String }, { .name="function_name", .type=LT::String } };

// Completion module params
static constexpr ParamDescriptor CompletionEntryParams[] = { { .name="text", .type=LT::String } };
static constexpr ParamDescriptor CompletionDescribedParams[] = { { .name="text", .type=LT::String }, { .name="description", .type=LT::String } };
static constexpr ParamDescriptor CompletionDetailedParams[] = { { .name="text", .type=LT::String }, { .name="description", .type=LT::String }, { .name="detail", .type=LT::String } };

// File I/O params
static constexpr ParamDescriptor FileOpenParams[] = { { .name="path", .type=LT::String }, { .name="mode", .type=LT::String } };
static constexpr ParamDescriptor FdNumberParam[] = { { .name="fd", .type=LT::Number } };
static constexpr ParamDescriptor PathStringParam[] = { { .name="path", .type=LT::String } };
static constexpr ParamDescriptor PathJoinParams[] = { { .name="base", .type=LT::String }, { .name="tail", .type=LT::String } };
static constexpr ParamDescriptor FileWriteParams[] = { { .name="path", .type=LT::String }, { .name="content", .type=LT::String } };

// Process signal params
static constexpr ParamDescriptor ProcessPidParam[] = { { .name="pid", .type=LT::Number } };

// Ref cell params
static constexpr ParamDescriptor RefObjParam[] = { { .name="ref", .type=LT::Number } };
static constexpr ParamDescriptor ProcessSignalParams[] = { { .name="signum", .type=LT::Number }, { .name="pid", .type=LT::Number } };

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
    StdlibDescriptor { .userFacingName="", .vmName="print", .returnType=LT::Void, .params=TextStringParam, .sharedImpl=nullptr, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="println", .returnType=LT::Void, .params=TextStringParam, .sharedImpl=nullptr, .description="", .detail="" },

    // -----------------------------------------------------------------------
    // Type Conversion (IR-generated, no VM registration)
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="string_length", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="string_length s -> int",
        .detail="**string_length** `s -> int`\n\nReturns the number of grapheme clusters (user-perceived characters) in string **s**." },
    StdlibDescriptor { .userFacingName="int_of_string", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="int_of_string s -> int",
        .detail="**int_of_string** `s -> int`\n\nParses string **s** as an integer." },
    StdlibDescriptor { .userFacingName="string_of_int", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="string_of_int n -> string",
        .detail="**string_of_int** `n -> string`\n\nConverts integer **n** to its string representation." },
    StdlibDescriptor { .userFacingName="not", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="not b -> bool",
        .detail="**not** `b -> bool`\n\nLogical negation of boolean **b**." },
    StdlibDescriptor { .userFacingName="force", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="force lazy<'T> -> 'T",
        .detail="**force** `lazy<'T> -> 'T`\n\nForces evaluation of a lazy value. First call evaluates and caches the result; subsequent calls return the cached value." },

    // -----------------------------------------------------------------------
    // String Operations
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="trim", .vmName="string_trim", .returnType=LT::String, .params=TextStringParam, .sharedImpl=&builtins::stringTrim,
        .description="trim s -> string",
        .detail="**trim** `s -> string`\n\nRemoves leading and trailing whitespace from **s**." },
    StdlibDescriptor { .userFacingName="toLower", .vmName="string_toLower", .returnType=LT::String, .params=TextStringParam, .sharedImpl=&builtins::stringToLower,
        .description="toLower s -> string",
        .detail="**toLower** `s -> string`\n\nConverts all characters in **s** to lowercase." },
    StdlibDescriptor { .userFacingName="toUpper", .vmName="string_toUpper", .returnType=LT::String, .params=TextStringParam, .sharedImpl=&builtins::stringToUpper,
        .description="toUpper s -> string",
        .detail="**toUpper** `s -> string`\n\nConverts all characters in **s** to uppercase." },
    StdlibDescriptor { .userFacingName="contains", .vmName="string_contains", .returnType=LT::Boolean, .params=StringContainsParams, .sharedImpl=&builtins::stringContains,
        .description="contains substr s -> bool",
        .detail="**contains** `substr s -> bool`\n\nReturns true if **s** contains **substr**." },
    StdlibDescriptor { .userFacingName="startsWith", .vmName="string_startsWith", .returnType=LT::Boolean, .params=StringStartsWithParams, .sharedImpl=&builtins::stringStartsWith,
        .description="startsWith prefix s -> bool",
        .detail="**startsWith** `prefix s -> bool`\n\nReturns true if **s** starts with **prefix**." },
    StdlibDescriptor { .userFacingName="endsWith", .vmName="string_endsWith", .returnType=LT::Boolean, .params=StringEndsWithParams, .sharedImpl=&builtins::stringEndsWith,
        .description="endsWith suffix s -> bool",
        .detail="**endsWith** `suffix s -> bool`\n\nReturns true if **s** ends with **suffix**." },
    StdlibDescriptor { .userFacingName="replace", .vmName="string_replace", .returnType=LT::String, .params=StringReplaceParams, .sharedImpl=&builtins::stringReplace,
        .description="replace old new s -> string",
        .detail="**replace** `old new s -> string`\n\nReplaces all occurrences of **old** with **new** in **s**." },
    StdlibDescriptor { .userFacingName="split", .vmName="string_split", .returnType=LT::Number, .params=StringSplitParams, .sharedImpl=&builtins::stringSplit,
        .description="split delim s -> list<string>",
        .detail="**split** `delim s -> list<string>`\n\nSplits **s** by delimiter **delim**." },
    StdlibDescriptor { .userFacingName="lines", .vmName="string_lines", .returnType=LT::Number, .params=StringLinesParam, .sharedImpl=&builtins::stringLines,
        .description="lines s -> list<string>",
        .detail="**lines** `s -> list<string>`\n\nSplits **s** into a list of lines (by newline, stripping `\\r`)." },
    StdlibDescriptor { .userFacingName="join", .vmName="string_join", .returnType=LT::String, .params=StringJoinParams, .sharedImpl=&builtins::stringJoin,
        .description="join delim lst -> string",
        .detail="**join** `delim lst -> string`\n\nJoins list elements with **delim** between them." },

    // -----------------------------------------------------------------------
    // String Unicode Decomposition
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="bytes", .vmName="string_to_bytes", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::stringToBytes,
        .description="bytes s -> list<int>",
        .detail="**bytes** `s -> list<int>`\n\nReturns the list of UTF-8 byte values (0–255) of string **s**." },
    StdlibDescriptor { .userFacingName="codepoints", .vmName="string_to_codepoints", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::stringToCodepoints,
        .description="codepoints s -> list<int>",
        .detail="**codepoints** `s -> list<int>`\n\nReturns the list of Unicode codepoint values of string **s**." },
    StdlibDescriptor { .userFacingName="graphemes", .vmName="string_to_graphemes", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::stringToGraphemes,
        .description="graphemes s -> list<string>",
        .detail="**graphemes** `s -> list<string>`\n\nReturns the list of grapheme clusters (user-perceived characters) of string **s**." },
    StdlibDescriptor { .userFacingName="byte_length", .vmName="string_byte_length", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::stringByteLength,
        .description="byte_length s -> int",
        .detail="**byte_length** `s -> int`\n\nReturns the number of UTF-8 bytes in string **s**." },
    StdlibDescriptor { .userFacingName="codepoint_length", .vmName="string_codepoint_length", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::stringCodepointLength,
        .description="codepoint_length s -> int",
        .detail="**codepoint_length** `s -> int`\n\nReturns the number of Unicode codepoints in string **s**." },
    StdlibDescriptor { .userFacingName="grapheme_length", .vmName="string_grapheme_length", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::stringGraphemeLength,
        .description="grapheme_length s -> int",
        .detail="**grapheme_length** `s -> int`\n\nReturns the number of grapheme clusters (user-perceived characters) in string **s**." },

    // -----------------------------------------------------------------------
    // List Basic
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="head", .vmName="list_head", .returnType=LT::Number, .params=ListNumberParam, .sharedImpl=&builtins::listHead,
        .description="head lst -> 'a",
        .detail="**head** `lst -> 'a`\n\nReturns the first element of the list." },
    StdlibDescriptor { .userFacingName="tail", .vmName="list_tail", .returnType=LT::Number, .params=ListNumberParam, .sharedImpl=&builtins::listTail,
        .description="tail lst -> list<'a>",
        .detail="**tail** `lst -> list<'a>`\n\nReturns the list without its first element." },
    StdlibDescriptor { .userFacingName="length", .vmName="list_length", .returnType=LT::Number, .params=ListNumberParam, .sharedImpl=&builtins::listLength,
        .description="length lst -> int",
        .detail="**length** `lst -> int`\n\nReturns the number of elements in the list." },
    StdlibDescriptor { .userFacingName="isEmpty", .vmName="list_isEmpty", .returnType=LT::Boolean, .params=ListNumberParam, .sharedImpl=&builtins::listIsEmpty,
        .description="isEmpty lst -> bool",
        .detail="**isEmpty** `lst -> bool`\n\nReturns true if the list is empty." },
    StdlibDescriptor { .userFacingName="nth", .vmName="list_nth", .returnType=LT::Number, .params=ListNthParams, .sharedImpl=&builtins::listNth,
        .description="nth n lst -> 'a",
        .detail="**nth** `n lst -> 'a`\n\nReturns the element at index **n** (0-based)." },
    StdlibDescriptor { .userFacingName="last", .vmName="list_last", .returnType=LT::Number, .params=ListNumberParam, .sharedImpl=&builtins::listLast,
        .description="last lst -> 'a",
        .detail="**last** `lst -> 'a`\n\nReturns the last element of the list." },
    StdlibDescriptor { .userFacingName="replicate", .vmName="list_replicate", .returnType=LT::Number, .params=ListReplicateParams, .sharedImpl=&builtins::listReplicate,
        .description="replicate n x -> list<'a>",
        .detail="**replicate** `n x -> list<'a>`\n\nCreates a list of **n** copies of **x**." },

    // -----------------------------------------------------------------------
    // List HOFs (IR-generated, no VM registration)
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="map", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="map f lst -> list<'b>",
        .detail="**map** `f lst -> list<'b>`\n\nApplies function **f** to each element of the list." },
    StdlibDescriptor { .userFacingName="filter", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="filter pred lst -> list<'a>",
        .detail="**filter** `pred lst -> list<'a>`\n\nKeeps only elements satisfying **pred**." },
    StdlibDescriptor { .userFacingName="fold", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="fold f init lst -> 'b",
        .detail="**fold** `f init lst -> 'b`\n\nReduces the list from the left with **f** and initial value **init**." },
    StdlibDescriptor { .userFacingName="reduce", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="reduce f lst -> 'a",
        .detail="**reduce** `f lst -> 'a`\n\nReduces the list from the left with **f** using the first element as initial." },
    StdlibDescriptor { .userFacingName="find", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="find pred lst -> option<'a>",
        .detail="**find** `pred lst -> option<'a>`\n\nReturns `Some x` for the first element matching **pred**, or `None`." },
    StdlibDescriptor { .userFacingName="exists", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="exists pred lst -> bool",
        .detail="**exists** `pred lst -> bool`\n\nReturns true if any element satisfies **pred**." },
    StdlibDescriptor { .userFacingName="forall", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="forall pred lst -> bool",
        .detail="**forall** `pred lst -> bool`\n\nReturns true if all elements satisfy **pred**." },
    StdlibDescriptor { .userFacingName="each", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="each f lst -> unit",
        .detail="**each** `f lst -> unit`\n\nApplies **f** to each element for side effects." },

    // -----------------------------------------------------------------------
    // List Transforms
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="sort", .vmName="list_sort", .returnType=LT::Number, .params=ListNumberParam, .sharedImpl=&builtins::listSort,
        .description="sort lst -> list<'a>",
        .detail="**sort** `lst -> list<'a>`\n\nReturns the list sorted in ascending order." },
    StdlibDescriptor { .userFacingName="reverse", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="reverse lst -> list<'a>",
        .detail="**reverse** `lst -> list<'a>`\n\nReturns the list in reverse order." },
    StdlibDescriptor { .userFacingName="distinct", .vmName="list_distinct", .returnType=LT::Number, .params=ListNumberParam, .sharedImpl=&builtins::listDistinct,
        .description="distinct lst -> list<'a>",
        .detail="**distinct** `lst -> list<'a>`\n\nRemoves duplicate elements from the list." },
    StdlibDescriptor { .userFacingName="sortBy", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="sortBy f lst -> list<'a>",
        .detail="**sortBy** `f lst -> list<'a>`\n\nSorts the list by the key returned by **f**." },
    StdlibDescriptor { .userFacingName="groupBy", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="groupBy f lst -> list<list<'a>>",
        .detail="**groupBy** `f lst -> list<list<'a>>`\n\nGroups consecutive elements with equal keys from **f**." },
    StdlibDescriptor { .userFacingName="take", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="take n lst -> list<'a>",
        .detail="**take** `n lst -> list<'a>`\n\nReturns the first **n** elements of the list." },
    StdlibDescriptor { .userFacingName="drop", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="drop n lst -> list<'a>",
        .detail="**drop** `n lst -> list<'a>`\n\nSkips the first **n** elements and returns the rest." },
    StdlibDescriptor { .userFacingName="zip", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="zip lst1 lst2 -> list<'a * 'b>",
        .detail="**zip** `lst1 lst2 -> list<'a * 'b>`\n\nCombines two lists into a list of pairs." },
    StdlibDescriptor { .userFacingName="flatten", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="flatten lst -> list<'a>",
        .detail="**flatten** `lst -> list<'a>`\n\nFlattens a list of lists into a single list." },

    // -----------------------------------------------------------------------
    // Seq Conversions
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="toList", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="toList seq -> list<'a>",
        .detail="**toList** `seq -> list<'a>`\n\nForces a lazy sequence into an eagerly-evaluated list." },

    // -----------------------------------------------------------------------
    // Formatting Helpers
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="formatNumber", .vmName="format_number", .returnType=LT::String, .params=FormatNumberTwoParams, .sharedImpl=&builtins::formatNumber,
        .description="formatNumber sep n -> string  |  formatNumber n -> string (locale)",
        .detail="**formatNumber** `sep n -> string`\n\nFormats a number with thousands separator **sep**.\nAlso: `formatNumber n` uses locale default." },
    StdlibDescriptor { .userFacingName="formatDateTime", .vmName="format_datetime", .returnType=LT::String, .params=EpochNumberParam, .sharedImpl=&builtins::formatDatetime,
        .description="formatDateTime epoch -> string",
        .detail="**formatDateTime** `epoch -> string`\n\nFormats an epoch timestamp as a human-readable date/time." },
    StdlibDescriptor { .userFacingName="formatMode", .vmName="format_mode", .returnType=LT::String, .params=ModeNumberParam, .sharedImpl=&builtins::formatMode,
        .description="formatMode mode -> string (rwxrwxrwx)",
        .detail="**formatMode** `mode -> string`\n\nFormats a file mode as `rwxrwxrwx` permission string." },
    StdlibDescriptor { .userFacingName="toText", .vmName="list_to_string", .returnType=LT::String, .params=ObjNumberParam, .sharedImpl=&builtins::listToString,
        .description="toText obj -> string",
        .detail="**toText** `obj -> string`\n\nConverts a structured object to a text representation." },
    StdlibDescriptor { .userFacingName="string", .vmName="object_to_string", .returnType=LT::String, .params=ObjNumberParam, .sharedImpl=&builtins::objectToString,
        .description="string x -> string",
        .detail="**string** `x -> string`\n\nConverts any value to its string representation." },

    // -----------------------------------------------------------------------
    // Permission Tests
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="isReadable", .vmName="mode_isReadable", .returnType=LT::Boolean, .params=ModeNumberParam, .sharedImpl=&builtins::modeIsReadable,
        .description="isReadable mode -> bool",
        .detail="**isReadable** `mode -> bool`\n\nReturns true if the file mode indicates read permission." },
    StdlibDescriptor { .userFacingName="isWritable", .vmName="mode_isWritable", .returnType=LT::Boolean, .params=ModeNumberParam, .sharedImpl=&builtins::modeIsWritable,
        .description="isWritable mode -> bool",
        .detail="**isWritable** `mode -> bool`\n\nReturns true if the file mode indicates write permission." },
    StdlibDescriptor { .userFacingName="isExecutable", .vmName="mode_isExecutable", .returnType=LT::Boolean, .params=ModeNumberParam, .sharedImpl=&builtins::modeIsExecutable,
        .description="isExecutable mode -> bool",
        .detail="**isExecutable** `mode -> bool`\n\nReturns true if the file mode indicates execute permission." },

    // -----------------------------------------------------------------------
    // Environment/System (user-facing, Shell provides callbacks)
    // -----------------------------------------------------------------------
    StdlibDescriptor { .userFacingName="env", .vmName="env.has", .returnType=LT::Boolean, .params=KeyStringParam, .sharedImpl=nullptr,
        .description="env name -> option<string>",
        .detail="**env** `name -> option<string>`\n\nLooks up environment variable **name**. Returns `Some value` or `None`." },
    StdlibDescriptor { .userFacingName="which", .vmName="which_find", .returnType=LT::Number, .params=ProgramStringParam, .sharedImpl=nullptr,
        .description="which name -> option<string>",
        .detail="**which** `name -> option<string>`\n\nFinds the full path of command **name** in `$PATH`." },
    StdlibDescriptor { .userFacingName="ps", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="ps -> list<ProcessInfo>",
        .detail="**ps** `-> list<ProcessInfo>`\n\nReturns a list of running processes with pid, user, cpu, mem, command fields." },
    StdlibDescriptor { .userFacingName="ls", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="ls -> list<FileInfo>  |  ls path -> list<FileInfo>",
        .detail="**ls** `-> list<FileInfo>`\n\nLists files in the current directory (or given **path**) as structured records." },
    StdlibDescriptor { .userFacingName="rand", .vmName="rand", .returnType=LT::Number, .params={}, .sharedImpl=&builtins::randNoArgs,
        .description="rand -> int  |  rand min max -> int",
        .detail="**rand** `-> int`\n\nReturns a random integer.\nAlso: `rand min max` for a random integer in range." },
    StdlibDescriptor { .userFacingName="fetch", .vmName="fetch", .returnType=LT::Number, .params=UrlStringParam, .sharedImpl=nullptr,
        .description="fetch url -> result<string, string>",
        .detail="**fetch** `url -> result<string, string>`\n\nFetches content from **url**. Returns `Ok body` or `Error msg`." },
    // httpServe — signature-only registration (real implementation is Shell::builtinHttpServe,
    // registered directly in Registration.cpp because it needs the Shell instance). This entry
    // registers the httpServe(IH)I signature for the compiler/test runtime and drives LSP docs.
    StdlibDescriptor { .userFacingName="httpServe", .vmName="httpServe", .returnType=LT::Number, .params=HttpServeParams, .sharedImpl=nullptr,
        .description="httpServe port handler -> int",
        .detail="**httpServe** `port -> (string -> string) -> int`\n\nStarts an HTTP server on **port**; **handler** maps a request path to a response body. Blocks serving connections until interrupted (Ctrl+C), then returns 0." },
    // register_completer — kept for backward compatibility but no longer user-facing
    // (use Completion.register instead)
    StdlibDescriptor { .userFacingName="", .vmName="", .returnType=LT::Void, .params=RegisterCompleterParams, .sharedImpl=nullptr,
        .description="", .detail="" },

    // -----------------------------------------------------------------------
    // Internal-only entries (no user-facing name)
    // -----------------------------------------------------------------------

    // env.get — separate VM registration
    StdlibDescriptor { .userFacingName="", .vmName="env.get", .returnType=LT::String, .params=KeyStringParam, .sharedImpl=nullptr, .description="", .detail="" },

    // export — F#-style overloads
    StdlibDescriptor { .userFacingName="", .vmName="export", .returnType=LT::Void, .params=ExportTwoParams, .sharedImpl=nullptr, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="export", .returnType=LT::Void, .params=NameStringParam, .sharedImpl=nullptr, .description="", .detail="" },

    // display_result — Shell/REPL only
    StdlibDescriptor { .userFacingName="", .vmName="display_result", .returnType=LT::Void, .params=ValueNumberParam, .sharedImpl=nullptr, .description="", .detail="" },

    // Completion module constructors (accessed via Completion.entry/described/detailed)
    StdlibDescriptor { .userFacingName="", .vmName="completer_register", .returnType=LT::Void, .params=RegisterCompleterParams,
        .sharedImpl=[](CoreVM::Params&){}, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="completion_entry", .returnType=LT::Number, .params=CompletionEntryParams, .sharedImpl=&builtins::completionEntry, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="completion_described", .returnType=LT::Number, .params=CompletionDescribedParams, .sharedImpl=&builtins::completionDescribed, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="completion_detailed", .returnType=LT::Number, .params=CompletionDetailedParams, .sharedImpl=&builtins::completionDetailed, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="completion_text", .returnType=LT::String, .params=ObjNumberParam, .sharedImpl=&builtins::completionText, .description="", .detail="" },

    // Multi-arity overloads
    StdlibDescriptor { .userFacingName="", .vmName="format_number", .returnType=LT::String, .params=FormatNumberOneParams, .sharedImpl=&builtins::formatNumberWithLocale, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="rand", .returnType=LT::Number, .params=RandRangeParams, .sharedImpl=&builtins::randRange, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="fetch", .returnType=LT::Number, .params=FetchTwoParams, .sharedImpl=nullptr, .description="", .detail="" },

    // Internal list operations
    StdlibDescriptor { .userFacingName="", .vmName="list_concat", .returnType=LT::Number, .params=ListConcatParams, .sharedImpl=&builtins::listConcat, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="list_sort_pairs", .returnType=LT::Number, .params=PairsNumberParam, .sharedImpl=&builtins::listSortPairs, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="list_group_pairs", .returnType=LT::Number, .params=PairsNumberParam, .sharedImpl=&builtins::listGroupPairs, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="list_char_range", .returnType=LT::Number, .params=ListCharRangeParams, .sharedImpl=&builtins::listCharRange, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="list_range", .returnType=LT::Number, .params=ListRangeParams, .sharedImpl=&builtins::listRange, .description="", .detail="" },

    // Internal string operations
    StdlibDescriptor { .userFacingName="", .vmName="string_repeat", .returnType=LT::String, .params=StringRepeatParams, .sharedImpl=&builtins::stringRepeat, .description="", .detail="" },

    // FileMode operations
    StdlibDescriptor { .userFacingName="", .vmName="filemode_from_bits", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::fileModeFromBits, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="filemode_is_readable", .returnType=LT::Boolean, .params=ObjNumberParam, .sharedImpl=&builtins::fileModeIsReadable, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="filemode_is_writable", .returnType=LT::Boolean, .params=ObjNumberParam, .sharedImpl=&builtins::fileModeIsWritable, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="filemode_is_executable", .returnType=LT::Boolean, .params=ObjNumberParam, .sharedImpl=&builtins::fileModeIsExecutable, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="filemode_owner", .returnType=LT::Number, .params=ObjNumberParam, .sharedImpl=&builtins::fileModeOwner, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="filemode_group", .returnType=LT::Number, .params=ObjNumberParam, .sharedImpl=&builtins::fileModeGroup, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="filemode_other", .returnType=LT::Number, .params=ObjNumberParam, .sharedImpl=&builtins::fileModeOther, .description="", .detail="" },

    // TimeSpan operations
    StdlibDescriptor { .userFacingName="", .vmName="timespan_from_ms", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::timespanFromMs, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="timespan_from_seconds", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::timespanFromSeconds, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="timespan_from_minutes", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::timespanFromMinutes, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="timespan_from_hours", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::timespanFromHours, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="timespan_from_days", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::timespanFromDays, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="sleep", .vmName="timespan_sleep", .returnType=LT::Number, .params=ObjNumberParam, .sharedImpl=&builtins::timespanSleep,
        .description="sleep ts -> unit",
        .detail="**sleep** `TimeSpan -> unit`\n\nPauses execution for the given TimeSpan duration." },
    StdlibDescriptor { .userFacingName="formatTimeSpan", .vmName="format_timespan", .returnType=LT::String, .params=ObjNumberParam, .sharedImpl=&builtins::formatTimeSpan,
        .description="formatTimeSpan ts -> string",
        .detail="**formatTimeSpan** `TimeSpan -> string`\n\nFormats a TimeSpan as a human-readable duration string." },

    // Size operations
    StdlibDescriptor { .userFacingName="", .vmName="size_from_bytes", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::sizeFromBytes, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="size_from_kb", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::sizeFromKB, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="size_from_mb", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::sizeFromMB, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="size_from_gb", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::sizeFromGB, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="size_from_tb", .returnType=LT::Number, .params=NNumberParam, .sharedImpl=&builtins::sizeFromTB, .description="", .detail="" },

    // Timing
    StdlibDescriptor { .userFacingName="", .vmName="__monotonic_ms", .returnType=LT::Number, .params={}, .sharedImpl=&builtins::monotonicMs, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="time", .vmName="", .returnType=LT::Void, .params={}, .sharedImpl=nullptr,
        .description="time { body } -> TimeSpan",
        .detail="**time** `{ body } -> TimeSpan`\n\n"
        "Measures the execution time of a computation expression and returns a TimeSpan.\n\n"
        "```endo\ntime { sleep (TimeSpan.fromSeconds 1) }\n```" },

    // DateTime operations
    StdlibDescriptor { .userFacingName="", .vmName="datetime_now", .returnType=LT::Number, .params={}, .sharedImpl=&builtins::dateTimeNow, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="datetime_from_epoch", .returnType=LT::Number, .params=EpochNumberParam, .sharedImpl=&builtins::dateTimeFromEpoch, .description="", .detail="" },

    // Markdown operations
    StdlibDescriptor { .userFacingName="markdown", .vmName="markdown_create", .returnType=LT::Number, .params=TextStringParam, .sharedImpl=&builtins::markdownCreate,
        .description="markdown text -> Markdown",
        .detail="**markdown** `text -> Markdown`\n\nCreates a Markdown object from a string." },
    StdlibDescriptor { .userFacingName="", .vmName="markdown_to_html", .returnType=LT::String, .params=MdNumberParam, .sharedImpl=&builtins::markdownToHtml, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="markdown_to_text", .returnType=LT::String, .params=MdNumberParam, .sharedImpl=&builtins::markdownToText, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="markdown_content", .returnType=LT::String, .params=MdNumberParam, .sharedImpl=&builtins::markdownContent, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="markdown_render", .returnType=LT::Void, .params=MdNumberParam, .sharedImpl=nullptr, .description="", .detail="" },

    // Json operations
    StdlibDescriptor { .userFacingName="", .vmName="json_query", .returnType=LT::Number, .params=JsonQueryParams, .sharedImpl=&builtins::jsonQuery,
        .description="Json.query path json -> list<string>",
        .detail="**Json.query** `path json -> list<string>`\n\n"
        "Extracts values from a JSON string using a dotted path.\n\n"
        "Path syntax: `.key` accesses an object property, `[]` iterates array elements.\n"
        "Example: `Json.query \".presets[].name\" json_str`" },

    // Path operations
    StdlibDescriptor { .userFacingName="", .vmName="path_temporary_directory", .returnType=LT::String, .params={}, .sharedImpl=&builtins::pathTemporaryDirectory, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="path_join", .returnType=LT::String, .params=PathJoinParams, .sharedImpl=&builtins::pathJoin,
        .description="Path.join a b -> string",
        .detail="**Path.join** `a b -> string`\n\nJoins two path segments with the platform-native separator. If **b** is absolute, it replaces **a**." },
    StdlibDescriptor { .userFacingName="", .vmName="path_dirname", .returnType=LT::String, .params=PathStringParam, .sharedImpl=&builtins::pathDirname,
        .description="Path.dirname p -> string",
        .detail="**Path.dirname** `p -> string`\n\nReturns the parent-directory portion of path **p**." },
    StdlibDescriptor { .userFacingName="", .vmName="path_basename", .returnType=LT::String, .params=PathStringParam, .sharedImpl=&builtins::pathBasename,
        .description="Path.basename p -> string",
        .detail="**Path.basename** `p -> string`\n\nReturns the final component (file name) of path **p**." },
    StdlibDescriptor { .userFacingName="", .vmName="path_normalize", .returnType=LT::String, .params=PathStringParam, .sharedImpl=&builtins::pathNormalize,
        .description="Path.normalize p -> string",
        .detail="**Path.normalize** `p -> string`\n\nLexically collapses `.`/`..` segments and normalizes separators. Does not touch the filesystem." },
    StdlibDescriptor { .userFacingName="", .vmName="path_is_absolute", .returnType=LT::Boolean, .params=PathStringParam, .sharedImpl=&builtins::pathIsAbsolute,
        .description="Path.isAbsolute p -> bool",
        .detail="**Path.isAbsolute** `p -> bool`\n\nReturns true if path **p** is absolute." },
    StdlibDescriptor { .userFacingName="", .vmName="path_separator", .returnType=LT::String, .params={}, .sharedImpl=&builtins::pathSeparator, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="path_delimiter", .returnType=LT::String, .params={}, .sharedImpl=&builtins::pathDelimiter, .description="", .detail="" },

    // File I/O operations
    StdlibDescriptor { .userFacingName="", .vmName="file_open", .returnType=LT::Number, .params=FileOpenParams, .sharedImpl=&builtins::fileOpen, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_close", .returnType=LT::Void, .params=FdNumberParam, .sharedImpl=&builtins::fileClose, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_read_line", .returnType=LT::Number, .params=FdNumberParam, .sharedImpl=&builtins::fileReadLine, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_read_all", .returnType=LT::Number, .params=PathStringParam, .sharedImpl=&builtins::fileReadAll, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_write_all", .returnType=LT::Number, .params=FileWriteParams, .sharedImpl=&builtins::fileWriteAll, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_append_all", .returnType=LT::Number, .params=FileWriteParams, .sharedImpl=&builtins::fileAppendAll, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_size", .returnType=LT::Number, .params=PathStringParam, .sharedImpl=&builtins::fileSize, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_exists", .returnType=LT::Boolean, .params=PathStringParam, .sharedImpl=&builtins::fileExists, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="file_delete", .returnType=LT::Number, .params=PathStringParam, .sharedImpl=&builtins::fileDelete, .description="", .detail="" },

    // Process signal operations
    StdlibDescriptor { .userFacingName="", .vmName="process_kill", .returnType=LT::Number, .params=ProcessPidParam, .sharedImpl=&builtins::processKill, .description="", .detail="" },
    StdlibDescriptor { .userFacingName="", .vmName="process_signal", .returnType=LT::Number, .params=ProcessSignalParams, .sharedImpl=&builtins::processSignal, .description="", .detail="" },

    // Ref cell write barrier
    StdlibDescriptor { .userFacingName="", .vmName="ref_write_barrier", .returnType=LT::Void, .params=RefObjParam, .sharedImpl=&builtins::refWriteBarrier, .description="", .detail="" },
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
