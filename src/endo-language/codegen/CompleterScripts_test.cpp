// SPDX-License-Identifier: Apache-2.0
#include <endo-language/TestHelper.hpp>
#include <endo-language/ast/AST.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

// =============================================================================
// Multi-line parser continuation tests — verify parsing succeeds
// =============================================================================

TEST_CASE("Completer.multiline.pipeline_parses", "[completer][parser]")
{
    // Pipeline |> on a new line should parse without errors
    CHECK(parse("let r = [1; 2; 3]\n  |> length") != nullptr);
}

TEST_CASE("Completer.multiline.chained_pipeline_parses", "[completer][parser]")
{
    // Chained |> across multiple lines
    CHECK(parse("let r = [1; 2; 3]\n  |> filter (fun x -> x > 1)\n  |> length") != nullptr);
}

TEST_CASE("Completer.multiline.list_concat_parses", "[completer][parser]")
{
    // @ on a new line should parse without errors
    CHECK(parse("let r =\n  [\"a\"; \"b\"]\n  @ [\"c\"; \"d\"]") != nullptr);
}

TEST_CASE("Completer.multiline.chained_list_concat_parses", "[completer][parser]")
{
    // Chained @ across multiple lines
    CHECK(parse("let r =\n  [\"a\"]\n  @ [\"b\"]\n  @ [\"c\"]") != nullptr);
}

TEST_CASE("Completer.multiline.cons_parses", "[completer][parser]")
{
    // :: on a new line should parse without errors
    CHECK(parse("let r =\n  1\n  :: [2; 3]") != nullptr);
}

TEST_CASE("Completer.multiline.or_pattern_parses", "[completer][parser]")
{
    // Or-pattern | alternation on a new line
    CHECK(parse("let r = match x with\n  | \"a\"\n  | \"b\" -> \"yes\"\n  | _ -> \"no\"") != nullptr);
}

TEST_CASE("Completer.multiline.pipeline_followed_by_statement", "[completer][parser]")
{
    // Pipeline on new line followed by a separate statement — newline pushback works
    CHECK(parse("let r = [1; 2; 3]\n  |> length\nprintln r") != nullptr);
}

TEST_CASE("Completer.multiline.concat_followed_by_statement", "[completer][parser]")
{
    // @ on new line followed by a separate statement
    CHECK(parse("let r =\n  [\"a\"]\n  @ [\"b\"]\nprintln r") != nullptr);
}

TEST_CASE("Completer.multiline.cons_followed_by_statement", "[completer][parser]")
{
    // :: on new line followed by a separate statement
    CHECK(parse("let r =\n  1\n  :: [2; 3]\nprintln r") != nullptr);
}

TEST_CASE("Completer.multiline.pipeline_no_spurious_continuation", "[completer][parser]")
{
    // A newline between two statements should NOT be consumed as a pipeline continuation.
    // `[1; 2; 3]` and `println "hello"` are separate statements.
    auto ast = parse("[1; 2; 3]\nprintln \"hello\"");
    CHECK(ast != nullptr);
}

// =============================================================================
// Multi-line execution tests — verify end-to-end works
// =============================================================================

TEST_CASE("Completer.multiline.pipeline_executes", "[completer][parser]")
{
    // Pipeline |> across lines with length
    CHECK(executeSourceAndGetOutput(R"(
        let r = [1; 2; 3]
          |> length
        print (toText r)
    )") == "3");
}

TEST_CASE("Completer.multiline.concat_executes", "[completer][parser]")
{
    // @ across lines — verify it produces a list
    CHECK(executesSuccessfully(R"(
        let r =
          ["a"; "b"]
          @ ["c"; "d"]
        print (toText (length r))
    )"));
}

TEST_CASE("Completer.multiline.cons_executes", "[completer][parser]")
{
    // :: across lines
    CHECK(executesSuccessfully("let r =\n  1\n  :: [2; 3]\nprint (toText r)"));
}

TEST_CASE("Completer.multiline.chained_concat_executes", "[completer][parser]")
{
    // Chained @ across lines — verify length
    CHECK(executeSourceAndGetOutput(R"(
        let r =
          [1]
          @ [2]
          @ [3]
        print (toText (length r))
    )") == "3");
}

// =============================================================================
// cmake completer logic tests — parts that work in test framework
// =============================================================================

TEST_CASE("Completer.cmake.options_list_not_empty", "[completer][cmake]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let cmake_options = [
          "--preset"; "--list-presets"; "--build"; "--install"
        ]
        print (toText (length cmake_options))
    )") == "4");
}

TEST_CASE("Completer.cmake.generator_list_length", "[completer][cmake]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let cmake_generators = ["Ninja"; "Ninja Multi-Config"; "Unix Makefiles"; "Watcom WMake"]
        print (toText (length cmake_generators))
    )") == "4");
}

TEST_CASE("Completer.cmake.configs_list", "[completer][cmake]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let cmake_configs = ["Debug"; "Release"; "RelWithDebInfo"; "MinSizeRel"]
        print (toText (length cmake_configs))
    )") == "4");
}

TEST_CASE("Completer.cmake.parse_presets_from_cmake_output", "[completer][cmake]")
{
    // Tests the parse_presets pipeline with mock cmake --list-presets output
    CHECK(executeSourceAndGetOutput(R"(
        let parse_presets raw =
            raw |> split "\n"
                |> filter (fun line -> contains "\"" line)
                |> map (fun line -> match split "\"" (trim line) with
                    | _ :: name :: _ -> name
                    | _ -> "")
                |> filter (fun s -> s != "")

        let output = "Available configure presets:\n\n  \"clang-debug\"  - Clang Debug\n  \"clang-release\" - Clang Release\n  \"gcc-debug\"    - GCC Debug\n"
        let r = parse_presets output
        print (toText (length r))
    )") == "3");
}

TEST_CASE("Completer.cmake.parse_presets_extracts_names", "[completer][cmake]")
{
    // Verify that parse_presets extracts the correct preset names
    CHECK(executeSourceAndGetOutput(R"(
        let parse_presets raw =
            raw |> split "\n"
                |> filter (fun line -> contains "\"" line)
                |> map (fun line -> match split "\"" (trim line) with
                    | _ :: name :: _ -> name
                    | _ -> "")
                |> filter (fun s -> s != "")

        let output = "Available configure presets:\n\n  \"clang-debug\"  - Clang Debug\n  \"gcc-release\"  - GCC Release\n"
        let r = parse_presets output
        each println r
    )") == "clang-debug\ngcc-release\n");
}

TEST_CASE("Completer.cmake.parse_presets_empty_output", "[completer][cmake]")
{
    // Empty cmake output should produce an empty list
    CHECK(executeSourceAndGetOutput(R"(
        let parse_presets raw =
            raw |> split "\n"
                |> filter (fun line -> contains "\"" line)
                |> map (fun line -> match split "\"" (trim line) with
                    | _ :: name :: _ -> name
                    | _ -> "")
                |> filter (fun s -> s != "")

        let r = parse_presets ""
        print (toText (length r))
    )") == "0");
}

TEST_CASE("Completer.cmake.parse_presets_no_quoted_lines", "[completer][cmake]")
{
    // Output with no quoted preset names (e.g., just headers) should produce empty list
    CHECK(executeSourceAndGetOutput(R"(
        let parse_presets raw =
            raw |> split "\n"
                |> filter (fun line -> contains "\"" line)
                |> map (fun line -> match split "\"" (trim line) with
                    | _ :: name :: _ -> name
                    | _ -> "")
                |> filter (fun s -> s != "")

        let output = "Available configure presets:\n\nNo presets found.\n"
        let r = parse_presets output
        print (toText (length r))
    )") == "0");
}

TEST_CASE("Completer.cmake.simple_function_call", "[completer][cmake]")
{
    // Simple function with match on integer patterns — verify list length
    CHECK(executeSourceAndGetOutput(R"(
        let cmake_configs = [1; 2]
        let select_config n =
          match n with
          | 1 -> cmake_configs
          | _ -> []

        print (toText (length (select_config 1)))
    )") == "2");
}

// =============================================================================
// ctest completer logic tests
// =============================================================================

TEST_CASE("Completer.ctest.parse_presets_from_ctest_output", "[completer][ctest]")
{
    // Tests the parse_presets pipeline with mock ctest --list-presets output
    CHECK(executeSourceAndGetOutput(R"(
        let parse_presets raw =
            raw |> split "\n"
                |> filter (fun line -> contains "\"" line)
                |> map (fun line -> match split "\"" (trim line) with
                    | _ :: name :: _ -> name
                    | _ -> "")
                |> filter (fun s -> s != "")

        let output = "Available test presets:\n\n  \"clang-debug\" - Clang Debug\n  \"clang-release\" - Clang Release\n"
        let r = parse_presets output
        print (toText (length r))
    )") == "2");
}

TEST_CASE("Completer.ctest.parse_presets_extracts_names", "[completer][ctest]")
{
    // Verify ctest parse_presets extracts the correct preset names
    CHECK(executeSourceAndGetOutput(R"(
        let parse_presets raw =
            raw |> split "\n"
                |> filter (fun line -> contains "\"" line)
                |> map (fun line -> match split "\"" (trim line) with
                    | _ :: name :: _ -> name
                    | _ -> "")
                |> filter (fun s -> s != "")

        let output = "Available test presets:\n\n  \"test-clang\"  - Clang Tests\n  \"test-gcc\"    - GCC Tests\n"
        let r = parse_presets output
        each println r
    )") == "test-clang\ntest-gcc\n");
}

TEST_CASE("Completer.ctest.options_list_length", "[completer][ctest]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let ctest_options = [
          "--preset"; "--list-presets"; "--parallel"; "-j";
          "--build-config"; "-C"; "--test-dir";
          "--output-on-failure"; "--stop-on-failure";
          "-R"; "-E"; "-L"; "--verbose"; "-V";
          "--timeout"; "--repeat"; "--rerun-failed"
        ]
        print (toText (length ctest_options))
    )") == "17");
}

// =============================================================================
// ssh/scp completer logic tests
// =============================================================================

TEST_CASE("Completer.ssh.options_list_not_empty", "[completer][ssh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let ssh_options = [
          "-p"; "-i"; "-l"; "-L"; "-R"; "-D"; "-J"; "-F"; "-o";
          "-N"; "-T"; "-t"; "-v"; "-X"; "-A"; "-C"; "-q"; "-4"; "-6"
        ]
        print (toText (length ssh_options))
    )") == "19");
}

TEST_CASE("Completer.ssh.split_pipeline_basic", "[completer][ssh]")
{
    // Minimal: split a string and iterate with each println
    CHECK(executeSourceAndGetOutput(R"(
        let lines = split "\n" "Host darkleon\nHost bravo"
        each println lines
    )") == "Host darkleon\nHost bravo\n");
}

TEST_CASE("Completer.ssh.split_pipeline_with_pipe_op", "[completer][ssh]")
{
    // Use |> to pipe into split
    CHECK(executeSourceAndGetOutput(R"(
        let lines = "Host darkleon\nHost bravo" |> split "\n"
        each println lines
    )") == "Host darkleon\nHost bravo\n");
}

TEST_CASE("Completer.ssh.split_pipeline_with_filter", "[completer][ssh]")
{
    // Split + filter empty strings
    CHECK(executeSourceAndGetOutput(R"(
        let lines = "Host darkleon\nHost bravo" |> split "\n" |> filter (fun s -> s != "")
        each println lines
    )") == "Host darkleon\nHost bravo\n");
}

TEST_CASE("Completer.ssh.map_with_match_split", "[completer][ssh]")
{
    // Simplest: map with match split inside lambda
    CHECK(executeSourceAndGetOutput(R"(
        let extract line =
            match split " " line with
            | _ :: host :: _ -> host
            | _ -> ""

        print (extract "Host darkleon")
    )") == "darkleon");
}

TEST_CASE("Completer.ssh.map_identity_on_string_list", "[completer][ssh]")
{
    // Map identity over a list of strings
    CHECK(executeSourceAndGetOutput(R"(
        let id x = x
        let hosts = map id ["darkleon"; "bravo"]
        each println hosts
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.map_with_split_no_match", "[completer][ssh]")
{
    // Map a function that calls split (without match) over a list
    CHECK(executeSourceAndGetOutput(R"(
        let extract line = split " " line
        let result = map extract ["Host darkleon"]
        print (toText (length result))
    )") == "1");
}

TEST_CASE("Completer.ssh.match_cons_number", "[completer][ssh]")
{
    // Match on a number list — verify extraction works at all
    CHECK(executeSourceAndGetOutput(R"(
        let second xs =
            match xs with
            | _ :: y :: _ -> y
            | _ -> 0

        print (toText (second [10; 20; 30]))
    )") == "20");
}

TEST_CASE("Completer.ssh.match_cons_string_direct", "[completer][ssh]")
{
    // Direct match on a string list (not through a function)
    CHECK(executeSourceAndGetOutput(R"(
        let xs = ["a"; "b"; "c"]
        let r = match xs with
            | _ :: y :: _ -> y
            | _ -> ""
        print r
    )") == "b");
}

TEST_CASE("Completer.ssh.match_cons_string_let_result", "[completer][ssh]")
{
    // Match on a string list through a function call, bind result to let
    CHECK(executeSourceAndGetOutput(R"(
        let second xs =
            match xs with
            | _ :: y :: _ -> y
            | _ -> ""

        let r = second ["a"; "b"; "c"]
        print r
    )") == "b");
}

TEST_CASE("Completer.ssh.match_cons_string_function", "[completer][ssh]")
{
    // Match on a string list through a function call, print directly
    CHECK(executeSourceAndGetOutput(R"(
        let second xs =
            match xs with
            | _ :: y :: _ -> y
            | _ -> ""

        print (second ["a"; "b"; "c"])
    )") == "b");
}

TEST_CASE("Completer.ssh.match_head_string_function", "[completer][ssh]")
{
    // Single-level cons match (simpler)
    CHECK(executeSourceAndGetOutput(R"(
        let hd xs =
            match xs with
            | x :: _ -> x
            | _ -> ""

        print (hd ["hello"; "world"])
    )") == "hello");
}

TEST_CASE("Completer.ssh.match_head_number_function", "[completer][ssh]")
{
    // Match on number list through function
    CHECK(executeSourceAndGetOutput(R"(
        let hd xs =
            match xs with
            | x :: _ -> x
            | _ -> 0

        print (toText (hd [42; 99]))
    )") == "42");
}

TEST_CASE("Completer.ssh.function_return_string_direct", "[completer][ssh]")
{
    // Function that returns a literal string — should work fine
    CHECK(executeSourceAndGetOutput(R"(
        let f _ = "hello"
        print (f 0)
    )") == "hello");
}

TEST_CASE("Completer.ssh.function_match_string_literal", "[completer][ssh]")
{
    // Function with match that returns string literals (no list extraction)
    CHECK(executeSourceAndGetOutput(R"(
        let f x =
            match x with
            | 1 -> "one"
            | _ -> "other"
        print (f 1)
    )") == "one");
}

TEST_CASE("Completer.ssh.function_match_returns_string_from_param", "[completer][ssh]")
{
    // Function with match on number, returning the string parameter
    CHECK(executeSourceAndGetOutput(R"(
        let f s =
            match 1 with
            | 1 -> s
            | _ -> ""
        print (f "hello")
    )") == "hello");
}

TEST_CASE("Completer.ssh.map_with_match_cons_over_list", "[completer][ssh]")
{
    // Map second over list of lists
    CHECK(executeSourceAndGetOutput(R"(
        let second xs =
            match xs with
            | _ :: y :: _ -> y
            | _ -> ""

        let extract line =
            second (split " " line)

        print (extract "Host darkleon")
    )") == "darkleon");
}

TEST_CASE("Completer.ssh.map_extract_over_list", "[completer][ssh]")
{
    // Map extract (using second + split) over a list of strings
    CHECK(executeSourceAndGetOutput(R"(
        let second xs =
            match xs with
            | _ :: y :: _ -> y
            | _ -> ""

        let extract line =
            second (split " " line)

        let hosts = map extract ["Host darkleon"; "Host bravo"]
        each println hosts
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.map_with_match_split_list", "[completer][ssh]")
{
    // Map extract over a list
    CHECK(executeSourceAndGetOutput(R"(
        let extract line =
            match split " " line with
            | _ :: host :: _ -> host
            | _ -> ""

        let hosts = map extract ["Host darkleon"; "Host bravo"]
        each println hosts
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.map_with_match_split_length", "[completer][ssh]")
{
    // Test map without printing elements
    CHECK(executeSourceAndGetOutput(R"(
        let extract line =
            match split " " line with
            | _ :: host :: _ -> host
            | _ -> ""

        let hosts = map extract ["Host darkleon"; "Host bravo"]
        println (length hosts)
    )") == "2\n");
}

TEST_CASE("Completer.ssh.map_identity_compiled_length", "[completer][ssh]")
{
    // Simplest compiled map: identity function on string list
    CHECK(executeSourceAndGetOutput(R"(
        let id (s : string) = s
        let hosts = map id ["a"; "b"; "c"]
        println (length hosts)
    )") == "3\n");
}

TEST_CASE("Completer.ssh.map_compiled_split_length", "[completer][ssh]")
{
    // Map with compiled function calling split
    CHECK(executeSourceAndGetOutput(R"(
        let f (line : string) = split " " line
        let hosts = map f ["Host darkleon"; "Host bravo"]
        println (length hosts)
    )") == "2\n");
}

TEST_CASE("Completer.ssh.map_compiled_match_number_length", "[completer][ssh]")
{
    // Map with match on numbers (no split, no list pattern)
    CHECK(executeSourceAndGetOutput(R"(
        let f (x : int) = match x with | 1 -> 10 | _ -> 0
        let hosts = map f [1; 2; 3]
        println (length hosts)
    )") == "3\n");
}

TEST_CASE("Completer.ssh.map_compiled_match_split_length", "[completer][ssh]")
{
    // Map with match on split result (the exact failing pattern)
    CHECK(executeSourceAndGetOutput(R"(
        let f line =
            match split " " line with
            | _ :: h :: _ -> h
            | _ -> ""
        let hosts = map f ["Host darkleon"; "Host bravo"; "Host charlie"]
        println (length hosts)
    )") == "3\n");
}

TEST_CASE("Completer.ssh.map_match_simple_cons_length", "[completer][ssh]")
{
    // Match on a pre-existing list (no split) inside map
    CHECK(executeSourceAndGetOutput(R"(
        let f xs =
            match xs with
            | a :: _ -> a
            | _ -> 0
        println (length (map f [[1;2]; [3;4]; [5;6]]))
    )") == "3\n");
}

TEST_CASE("Completer.ssh.map_match_wildcard_number", "[completer][ssh]")
{
    // Simple match with wildcard (no cons pattern)
    CHECK(executeSourceAndGetOutput(R"(
        let f x =
            match x with
            | 1 -> "one"
            | 2 -> "two"
            | _ -> "other"
        let r = map f [1; 2; 3]
        println (length r)
    )") == "3\n");
}

TEST_CASE("Completer.ssh.map_match_split_head_length", "[completer][ssh]")
{
    // Match on split with SINGLE cons (not double)
    CHECK(executeSourceAndGetOutput(R"(
        let f line =
            match split " " line with
            | h :: _ -> h
            | _ -> ""
        let r = map f ["Host darkleon"; "Host bravo"; "Host charlie"]
        println (length r)
    )") == "3\n");
}

TEST_CASE("Completer.ssh.map_match_split_length_only", "[completer][ssh]")
{
    // Match on split result with length check (no cons)
    CHECK(executeSourceAndGetOutput(R"(
        let f line =
            match split " " line with
            | _ -> "found"
        let r = map f ["Host darkleon"; "Host bravo"]
        println (length r)
    )") == "2\n");
}

TEST_CASE("Completer.ssh.cons_a_wildcard", "[completer][ssh]")
{
    // Match on a :: _ pattern (head extraction, tail wildcard)
    CHECK(executeSourceAndGetOutput(R"(
        let hosts = ["darkleon"; "bravo"]
        match hosts with
        | a :: _ -> println a
        | _ -> println "fail"
    )") == "darkleon\n");
}

TEST_CASE("Completer.ssh.cons_a_b_wildcard_multiline", "[completer][ssh]")
{
    // Match on a :: b :: _ with multi-line arm body
    CHECK(executeSourceAndGetOutput(R"(
        let hosts = ["darkleon"; "bravo"; "charlie"]
        match hosts with
        | a :: b :: _ ->
            println a
            println b
        | _ -> println "fail"
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.map_with_match_split_direct_print", "[completer][ssh]")
{
    // Test: print each element individually without `each`
    CHECK(executeSourceAndGetOutput(R"(
        let extract line =
            match split " " line with
            | _ :: host :: _ -> host
            | _ -> ""

        let hosts = map extract ["Host darkleon"; "Host bravo"]
        match hosts with
        | a :: b :: _ ->
            println a
            println b
        | _ -> println "fail"
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.split_pipeline_with_map", "[completer][ssh]")
{
    // Split + filter + map to extract host names
    CHECK(executeSourceAndGetOutput(R"(
        let hosts = "Host darkleon\nHost bravo" |> split "\n" |> filter (fun s -> s != "") |> map (fun line ->
            match split " " line with
            | _ :: host :: _ -> host
            | _ -> "")
        each println hosts
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.hosts_extraction_end_to_end", "[completer][ssh]")
{
    // Test the full ssh_complete expression path that executeCompleterFunction builds.
    // Uses a hardcoded string instead of $(grep ...) to avoid shell dependency in tests.
    CHECK(executeSourceAndGetOutput(R"(
        let ssh_hosts _ =
            "Host darkleon\nHost bravo" |> split "\n" |> filter (fun s -> s != "") |> map (fun line ->
                match split " " line with
                | _ :: host :: _ -> host
                | _ -> "") |> filter (fun s -> s != "") |> filter (fun s -> not (contains "*" s))

        let ssh_options = ["-p"; "-i"]

        let ssh_complete args prefix =
            match args with
            | _ when startsWith "-" prefix -> ssh_options
            | _ -> ssh_hosts ()

        ssh_complete [] "" |> each println
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.hosts_with_wildcard_filtering", "[completer][ssh]")
{
    // Ensure wildcard Host entries are filtered out
    CHECK(executeSourceAndGetOutput(R"(
        let ssh_hosts _ =
            "Host darkleon\nHost *\nHost bravo" |> split "\n" |> filter (fun s -> s != "") |> map (fun line ->
                match split " " line with
                | _ :: host :: _ -> host
                | _ -> "") |> filter (fun s -> s != "") |> filter (fun s -> not (contains "*" s))

        let ssh_options = ["-p"; "-i"]

        let ssh_complete args prefix =
            match args with
            | _ when startsWith "-" prefix -> ssh_options
            | _ -> ssh_hosts ()

        ssh_complete [] "" |> each println
    )") == "darkleon\nbravo\n");
}

TEST_CASE("Completer.ssh.option_completion", "[completer][ssh]")
{
    // When prefix starts with '-', return ssh_options
    CHECK(executeSourceAndGetOutput(R"(
        let ssh_hosts _ = ["darkleon"; "bravo"]
        let ssh_options = ["-p"; "-i"]

        let ssh_complete args prefix =
            match args with
            | _ when startsWith "-" prefix -> ssh_options
            | _ -> ssh_hosts ()

        ssh_complete [] "-" |> each println
    )") == "-p\n-i\n");
}

TEST_CASE("Completer.scp.options_count", "[completer][scp]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let scp_options = ["-P"; "-i"; "-F"; "-o"; "-r"; "-v"; "-C"; "-q"; "-4"; "-6"; "-3"]
        print (toText (length scp_options))
    )") == "11");
}

// =============================================================================
// flatpak completer logic tests
// =============================================================================

TEST_CASE("Completer.flatpak.subcommands_not_empty", "[completer][flatpak]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let flatpak_subcommands = [
          "run"; "install"; "uninstall"; "update";
          "list"; "info"; "search"; "override"
        ]
        print (toText (length flatpak_subcommands))
    )") == "8");
}

TEST_CASE("Completer.flatpak.options_length", "[completer][flatpak]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let flatpak_options = [
          "--user"; "--system"; "--installation"; "--verbose"; "-v";
          "--help"; "-h"; "--version"
        ]
        print (toText (length flatpak_options))
    )") == "8");
}

TEST_CASE("Completer.flatpak.subcommands_length", "[completer][flatpak]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let flatpak_subcommands = ["run"; "install"; "uninstall"; "update"]
        print (toText (length flatpak_subcommands))
    )") == "4");
}

TEST_CASE("Completer.flatpak.options_count", "[completer][flatpak]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let flatpak_options = ["--user"; "--system"; "--help"; "-h"; "--version"]
        print (toText (length flatpak_options))
    )") == "5");
}

TEST_CASE("Completer.flatpak.simple_function_with_match", "[completer][flatpak]")
{
    // Simple function with match on integer patterns — verify list length
    CHECK(executeSourceAndGetOutput(R"(
        let flatpak_subcommands = [1; 2; 3]
        let get_default n =
          match n with
          | 0 -> flatpak_subcommands
          | _ -> []

        print (toText (length (get_default 0)))
    )") == "3");
}

// =============================================================================
// claude completer logic tests
// =============================================================================

TEST_CASE("Completer.claude.options_list_not_empty", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let options = [
            "--help"; "-h"; "--version"; "--verbose"; "-v"; "--debug";
            "--model"; "--fallback-model"; "--permission-mode"; "--resume"; "-r";
            "--continue"; "-c"; "--print"; "-p"; "--output-format"; "--input-format";
            "--max-turns"; "--system-prompt"; "--append-system-prompt";
            "--allowedTools"; "--disallowedTools"; "--mcp-config";
            "--no-cache"; "--no-profile"; "--profile"; "--effort";
            "--yes"; "-y"
        ]
        print (toText (length options))
    )") == "29");
}

TEST_CASE("Completer.claude.model_completion", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let models = [
            "opus"; "sonnet"; "haiku";
            "claude-opus-4-6"; "claude-sonnet-4-6"; "claude-haiku-4-5-20251001"
        ]
        print (toText (length models))
    )") == "6");
}

TEST_CASE("Completer.claude.permission_mode_values", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let permission_modes = ["default"; "plan"; "auto"; "bypassPermissions"]
        each println permission_modes
    )") == "default\nplan\nauto\nbypassPermissions\n");
}

TEST_CASE("Completer.claude.subcommands_list", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let subcommands = [
            "mcp"; "auth"; "doctor"; "install"; "plugin"; "setup-token"; "update"
        ]
        print (toText (length subcommands))
    )") == "7");
}

TEST_CASE("Completer.claude.mcp_subcommands", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let mcp_subcommands = [
            "add"; "list"; "get"; "remove"; "serve"; "start"; "stop"; "reset"
        ]
        print (toText (length mcp_subcommands))
    )") == "8");
}

TEST_CASE("Completer.claude.effort_levels", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let effort_levels = ["low"; "medium"; "high"; "auto"]
        each println effort_levels
    )") == "low\nmedium\nhigh\nauto\n");
}

TEST_CASE("Completer.claude.output_format_values", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let output_formats = ["text"; "json"; "stream-json"]
        each println output_formats
    )") == "text\njson\nstream-json\n");
}

TEST_CASE("Completer.claude.complete_function_returns_subcommands", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let claude_complete args prefix =
            let subcommands = ["mcp"; "auth"; "doctor"; "install"; "plugin"; "setup-token"; "update"]
            let options = ["--help"; "--model"; "--resume"]
            match args with
            | [] when startsWith "-" prefix -> options
            | [] -> subcommands
            | _ -> []

        claude_complete [] "" |> each println
    )") == "mcp\nauth\ndoctor\ninstall\nplugin\nsetup-token\nupdate\n");
}

TEST_CASE("Completer.claude.complete_function_returns_options", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let claude_complete args prefix =
            let subcommands = ["mcp"; "auth"; "doctor"]
            let options = ["--help"; "--model"; "--resume"]
            match args with
            | [] when startsWith "-" prefix -> options
            | [] -> subcommands
            | _ -> []

        claude_complete [] "--" |> each println
    )") == "--help\n--model\n--resume\n");
}

TEST_CASE("Completer.claude.model_match_arm", "[completer][claude]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let claude_complete args prefix =
            let models = ["opus"; "sonnet"; "haiku"]
            match args with
            | ["--model"] -> models
            | _ -> []

        claude_complete ["--model"] "" |> each println
    )") == "opus\nsonnet\nhaiku\n");
}

// =============================================================================
// gh completer logic tests
// =============================================================================

TEST_CASE("Completer.gh.top_level_commands", "[completer][gh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let commands = [
            "auth"; "browse"; "codespace"; "gist"; "issue"; "pr"; "project"; "release";
            "repo"; "run"; "workflow"; "cache"; "alias"; "api"; "config"; "label";
            "search"; "secret"; "ssh-key"; "status"; "variable"; "extension";
            "gpg-key"; "completion"; "attestation"; "ruleset"; "copilot"; "org"
        ]
        print (toText (length commands))
    )") == "28");
}

TEST_CASE("Completer.gh.pr_subcommands", "[completer][gh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let pr_subcommands = [
            "checkout"; "checks"; "close"; "comment"; "create"; "diff"; "edit";
            "list"; "merge"; "ready"; "reopen"; "review"; "status"; "view"
        ]
        print (toText (length pr_subcommands))
    )") == "14");
}

TEST_CASE("Completer.gh.issue_subcommands", "[completer][gh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let issue_subcommands = [
            "close"; "comment"; "create"; "delete"; "develop"; "edit"; "list";
            "lock"; "pin"; "reopen"; "status"; "transfer"; "unpin"; "unlock"; "view"
        ]
        print (toText (length issue_subcommands))
    )") == "15");
}

TEST_CASE("Completer.gh.complete_function_returns_commands", "[completer][gh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let gh_complete args prefix =
            let commands = ["auth"; "issue"; "pr"; "repo"]
            let global_options = ["--help"; "--version"]
            match args with
            | [] when startsWith "-" prefix -> global_options
            | [] -> commands
            | _ -> []

        gh_complete [] "" |> each println
    )") == "auth\nissue\npr\nrepo\n");
}

TEST_CASE("Completer.gh.pr_create_options", "[completer][gh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let gh_complete args prefix =
            let pr_create_options = ["--title"; "--body"; "--draft"; "--fill"]
            match args with
            | ["pr"; "create"] when startsWith "-" prefix -> pr_create_options
            | _ -> []

        gh_complete ["pr"; "create"] "--" |> each println
    )") == "--title\n--body\n--draft\n--fill\n");
}

TEST_CASE("Completer.gh.pr_dispatch", "[completer][gh]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let gh_complete args prefix =
            let commands = ["auth"; "issue"; "pr"]
            let pr_subcommands = ["checkout"; "create"; "list"; "merge"; "view"]
            match args with
            | [] -> commands
            | ["pr"] -> pr_subcommands
            | _ -> []

        gh_complete ["pr"] "" |> each println
    )") == "checkout\ncreate\nlist\nmerge\nview\n");
}

// =============================================================================
// glab completer logic tests
// =============================================================================

TEST_CASE("Completer.glab.top_level_commands", "[completer][glab]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let commands = [
            "alias"; "api"; "auth"; "check-update"; "ci"; "completion"; "config";
            "help"; "incident"; "issue"; "label"; "mr"; "release"; "repo";
            "schedule"; "snippet"; "ssh-key"; "user"; "variable"; "version"
        ]
        print (toText (length commands))
    )") == "20");
}

TEST_CASE("Completer.glab.mr_subcommands", "[completer][glab]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let mr_subcommands = [
            "approve"; "checkout"; "close"; "create"; "delete"; "diff"; "for";
            "issues"; "list"; "merge"; "note"; "rebase"; "reopen"; "revoke";
            "subscribe"; "todo"; "unsubscribe"; "update"; "view"
        ]
        print (toText (length mr_subcommands))
    )") == "19");
}

TEST_CASE("Completer.glab.ci_alias_resolves", "[completer][glab]")
{
    // Verify that "pipe" and "pipeline" aliases resolve to "ci" subcommands
    CHECK(executeSourceAndGetOutput(R"(
        let ci_subcommands = [
            "artifact"; "delete"; "get"; "lint"; "list"; "retry"; "run";
            "status"; "trace"; "trigger"; "view"
        ]
        let resolve_alias cmd =
            match cmd with
            | "pipe" -> "ci"
            | "pipeline" -> "ci"
            | "project" -> "repo"
            | _ -> cmd

        let glab_complete args prefix =
            let resolved = match args with
                | cmd :: rest -> (resolve_alias cmd) :: rest
                | _ -> args
            match resolved with
            | ["ci"] -> ci_subcommands
            | _ -> []

        glab_complete ["pipe"] "" |> each println
    )") == "artifact\ndelete\nget\nlint\nlist\nretry\nrun\nstatus\ntrace\ntrigger\nview\n");
}

TEST_CASE("Completer.glab.pipeline_alias_resolves", "[completer][glab]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let ci_subcommands = ["lint"; "list"; "run"]
        let resolve_alias cmd =
            match cmd with
            | "pipe" -> "ci"
            | "pipeline" -> "ci"
            | _ -> cmd

        let glab_complete args prefix =
            let resolved = match args with
                | cmd :: rest -> (resolve_alias cmd) :: rest
                | _ -> args
            match resolved with
            | ["ci"] -> ci_subcommands
            | _ -> []

        glab_complete ["pipeline"] "" |> each println
    )") == "lint\nlist\nrun\n");
}

TEST_CASE("Completer.glab.mr_create_options", "[completer][glab]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let glab_complete args prefix =
            let mr_create_options = ["--title"; "--description"; "--draft"; "--fill"]
            match args with
            | ["mr"; "create"] when startsWith "-" prefix -> mr_create_options
            | _ -> []

        glab_complete ["mr"; "create"] "--" |> each println
    )") == "--title\n--description\n--draft\n--fill\n");
}

TEST_CASE("Completer.glab.project_alias_resolves", "[completer][glab]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let repo_subcommands = ["clone"; "create"; "list"]
        let resolve_alias cmd =
            match cmd with
            | "project" -> "repo"
            | _ -> cmd

        let glab_complete args prefix =
            let resolved = match args with
                | cmd :: rest -> (resolve_alias cmd) :: rest
                | _ -> args
            match resolved with
            | ["repo"] -> repo_subcommands
            | _ -> []

        glab_complete ["project"] "" |> each println
    )") == "clone\ncreate\nlist\n");
}

// =============================================================================
// Subcommand option completion tests — verify --<TAB> after subcommands works
// =============================================================================

TEST_CASE("Completer.claude.mcp_option_completion", "[completer][claude]")
{
    // `claude mcp --<TAB>` should return options, not mcp_subcommands
    CHECK(executeSourceAndGetOutput(R"(
        let claude_complete args prefix =
            let subcommands = ["mcp"; "auth"; "doctor"]
            let options = ["--help"; "--model"; "--resume"]
            let mcp_subcommands = ["add"; "list"; "get"; "remove"]
            match args with
            | [] when startsWith "-" prefix -> options
            | [] -> subcommands
            | [_] when startsWith "-" prefix -> options
            | ["mcp"] -> mcp_subcommands
            | _ when startsWith "-" prefix -> options
            | _ -> []

        claude_complete ["mcp"] "--" |> each println
    )") == "--help\n--model\n--resume\n");
}

TEST_CASE("Completer.gh.repo_option_completion", "[completer][gh]")
{
    // `gh repo --<TAB>` should return global_options, not repo_subcommands
    CHECK(executeSourceAndGetOutput(R"(
        let gh_complete args prefix =
            let commands = ["auth"; "issue"; "pr"; "repo"]
            let global_options = ["--help"; "--version"]
            let repo_subcommands = ["clone"; "create"; "list"]
            match args with
            | [] when startsWith "-" prefix -> global_options
            | [] -> commands
            | [_] when startsWith "-" prefix -> global_options
            | ["repo"] -> repo_subcommands
            | _ when startsWith "-" prefix -> global_options
            | _ -> []

        gh_complete ["repo"] "--" |> each println
    )") == "--help\n--version\n");
}

TEST_CASE("Completer.glab.ci_option_completion", "[completer][glab]")
{
    // `glab ci --<TAB>` should return global_options, not ci_subcommands
    CHECK(executeSourceAndGetOutput(R"(
        let glab_complete args prefix =
            let commands = ["ci"; "mr"; "issue"; "repo"]
            let global_options = ["--help"; "--version"]
            let ci_subcommands = ["lint"; "list"; "run"]
            match args with
            | [] when startsWith "-" prefix -> global_options
            | [] -> commands
            | [_] when startsWith "-" prefix -> global_options
            | ["ci"] -> ci_subcommands
            | _ when startsWith "-" prefix -> global_options
            | _ -> []

        glab_complete ["ci"] "--" |> each println
    )") == "--help\n--version\n");
}

// =============================================================================
// BlockExpr scope cleanup regression tests (heap-use-after-free fix)
// =============================================================================

TEST_CASE("Completer.blockexpr.return_first_let_bound_list", "[completer][blockexpr]")
{
    // Function with let-bound lists in BlockExpr body returning the first list
    CHECK(executeSourceAndGetOutput(R"(
        let select x =
            let a = [1; 2; 3]
            let b = [4; 5]
            match x with
            | 1 -> a
            | _ -> b

        print (toText (length (select 1)))
    )") == "3");
}

TEST_CASE("Completer.blockexpr.return_second_let_bound_list", "[completer][blockexpr]")
{
    // Function with let-bound lists in BlockExpr body returning the second list
    CHECK(executeSourceAndGetOutput(R"(
        let select x =
            let a = [1; 2; 3]
            let b = [4; 5]
            match x with
            | 1 -> a
            | _ -> b

        print (toText (length (select 0)))
    )") == "2");
}

TEST_CASE("Completer.blockexpr.return_string_list_from_block", "[completer][blockexpr]")
{
    // Mirrors flatpak completer pattern: let-bound string list returned from match
    CHECK(executeSourceAndGetOutput(R"(
        let flatpak_complete args =
            let subcommands = ["run"; "install"; "uninstall"; "update"]
            let options = ["--user"; "--system"]
            match args with
            | [] -> subcommands
            | _ -> options

        print (toText (length (flatpak_complete [])))
    )") == "4");
}

TEST_CASE("Completer.blockexpr.pipeline_each_after_block_return", "[completer][blockexpr]")
{
    // Full pipeline pattern: function returns let-bound list, piped to each println
    CHECK(executeSourceAndGetOutput(R"(
        let get_items args =
            let subcommands = ["run"; "install"; "update"]
            match args with
            | [] -> subcommands
            | _ -> []

        get_items [] |> each println
    )") == "run\ninstall\nupdate\n");
}

// =============================================================================
// register_completer verification tests
// =============================================================================

TEST_CASE("Completer.register_completer.valid_definition", "[completer][register]")
{
    CHECK(executesSuccessfully(R"(
        let my_complete args prefix = ["--help"; "--version"]
        register_completer "mytest" my_complete
    )"));
}

// =============================================================================
// Completer script parsing — verify actual .endo files parse without errors
// =============================================================================

#ifdef ENDO_COMPLETERS_DIR
    #include <filesystem>
    #include <fstream>

TEST_CASE("Completer.scripts.cmake_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "cmake.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.ctest_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "ctest.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.ssh_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "ssh.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.scp_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "scp.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.flatpak_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "flatpak.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.claude_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "claude.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.gh_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "gh.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.glab_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "glab.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    REQUIRE(ifs.is_open());
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}
#endif
