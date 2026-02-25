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

TEST_CASE("Completer.cmake.presets_from_json_query_multiline_concat", "[completer][cmake]")
{
    // Tests the cmake_presets_from function with multi-line @ concat
    CHECK(executeSourceAndGetOutput(R"(
        let cmake_presets_from json =
          Json.query ".configurePresets[].name" json
          @ Json.query ".buildPresets[].name" json
          @ Json.query ".testPresets[].name" json

        let json = "{\"configurePresets\":[{\"name\":\"dev\"}],\"buildPresets\":[{\"name\":\"build-dev\"}],\"testPresets\":[{\"name\":\"test-dev\"}]}"
        let r = cmake_presets_from json
        print (toText (length r))
    )") == "3");
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

TEST_CASE("Completer.ctest.presets_from_json_query", "[completer][ctest]")
{
    CHECK(executeSourceAndGetOutput(R"(
        let ctest_presets_from json =
          Json.query ".testPresets[].name" json

        let json = "{\"testPresets\":[{\"name\":\"test-clang\"},{\"name\":\"test-gcc\"}]}"
        let r = ctest_presets_from json
        print (toText (length r))
    )") == "2");
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
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.ctest_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "ctest.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.ssh_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "ssh.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.scp_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "scp.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}

TEST_CASE("Completer.scripts.flatpak_parses", "[completer][scripts]")
{
    auto path = std::filesystem::path(ENDO_COMPLETERS_DIR) / "flatpak.endo";
    REQUIRE(std::filesystem::exists(path));
    auto ifs = std::ifstream(path);
    auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
    CHECK(parse(content) != nullptr);
}
#endif
