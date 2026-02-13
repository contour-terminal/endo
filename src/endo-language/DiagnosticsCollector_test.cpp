// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <endo-language/DiagnosticsCollector.hpp>

using namespace endo;

TEST_CASE("DiagnosticsCollector.valid_source_no_diagnostics", "[diagnostics]")
{
    auto diagnostics = collectDiagnostics("let x = 42");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.valid_function_no_diagnostics", "[diagnostics]")
{
    auto diagnostics = collectDiagnostics("let add x y = x + y");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.invalid_let_binding", "[diagnostics]")
{
    auto diagnostics = collectDiagnostics("let = 42");
    REQUIRE(!diagnostics.empty());
    CHECK(!diagnostics[0].message.empty());
    CHECK(diagnostics[0].severity == DiagnosticSeverity::Error);
}

TEST_CASE("DiagnosticsCollector.diagnostic_has_range", "[diagnostics]")
{
    auto diagnostics = collectDiagnostics("let = 42");
    REQUIRE(!diagnostics.empty());
    // Range should be 0-based
    CHECK(diagnostics[0].range.start.line >= 0);
    CHECK(diagnostics[0].range.start.character >= 0);
}

TEST_CASE("DiagnosticsCollector.empty_source", "[diagnostics]")
{
    auto diagnostics = collectDiagnostics("");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.valid_match_expression", "[diagnostics]")
{
    auto diagnostics = collectDiagnostics("let x = match 1 with | 1 -> \"one\" | _ -> \"other\"");
    CHECK(diagnostics.empty());
}

// =============================================================================
// Command-not-found diagnostics
// =============================================================================

TEST_CASE("DiagnosticsCollector.known_builtin_no_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("cd /tmp");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.print_builtin_no_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("echo hello");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.path_command_no_diagnostic", "[diagnostics][command-not-found]")
{
    // ls should be in PATH on any Linux system
    auto diagnostics = collectDiagnostics("ls -la");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.unknown_command_produces_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("definitely_not_a_real_command_xyz");
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].severity == DiagnosticSeverity::Error);
    CHECK(diagnostics[0].message.find("command not found") != std::string::npos);
    CHECK(diagnostics[0].message.find("definitely_not_a_real_command_xyz") != std::string::npos);
}

TEST_CASE("DiagnosticsCollector.unknown_command_has_correct_range", "[diagnostics][command-not-found]")
{
    // Use a command followed by whitespace so the lexer end position is clean
    auto diagnostics = collectDiagnostics("fake_cmd hello");
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].range.start.line == 0);
    CHECK(diagnostics[0].range.start.character == 0);
    // "fake_cmd" is 8 characters at columns 0..7, so end should be 8
    CHECK(diagnostics[0].range.end.character == 8);
}

TEST_CASE("DiagnosticsCollector.explicit_path_no_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("./myscript");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.absolute_path_no_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("/usr/bin/something");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.fsharp_function_no_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("let greet name = println name\ngreet world");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.piped_unknown_commands_produce_diagnostics",
          "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("fake_cmd_aaa | fake_cmd_bbb");
    // Should produce diagnostics for both unknown commands
    auto commandNotFoundCount = 0;
    for (auto const& d: diagnostics)
    {
        if (d.message.find("command not found") != std::string::npos)
            commandNotFoundCount++;
    }
    CHECK(commandNotFoundCount == 2);
}

TEST_CASE("DiagnosticsCollector.fsharp_and_binding_no_diagnostic", "[diagnostics][command-not-found]")
{
    auto diagnostics = collectDiagnostics("let rec f x = x\nand g x = x\ng hello");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.persisted_fsharp_name_no_diagnostic", "[diagnostics][command-not-found]")
{
    // "f" is not defined in the current source, but is a known persisted F# function
    auto diagnostics = collectDiagnostics("f 42", { "f" });
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.persisted_fsharp_value_binding_no_diagnostic",
          "[diagnostics][command-not-found]")
{
    // "myval" is a persisted value binding from a prior REPL prompt
    auto diagnostics = collectDiagnostics("print myval", { "myval" });
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.unknown_command_still_flagged_with_known_names",
          "[diagnostics][command-not-found]")
{
    // "f" is known, but "unknown_cmd_xyz" is not — should still get a diagnostic
    auto diagnostics = collectDiagnostics("unknown_cmd_xyz", { "f" });
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].message.find("command not found") != std::string::npos);
}

// =============================================================================
// Shell builtin diagnostics (must not crash)
// =============================================================================

TEST_CASE("DiagnosticsCollector.which_no_args_no_crash", "[diagnostics][builtins]")
{
    auto diagnostics = collectDiagnostics("which");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.which_with_args_no_crash", "[diagnostics][builtins]")
{
    auto diagnostics = collectDiagnostics("which ls");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.bind_no_args_no_crash", "[diagnostics][builtins]")
{
    auto diagnostics = collectDiagnostics("bind");
    CHECK(diagnostics.empty());
}

TEST_CASE("DiagnosticsCollector.exit_no_crash", "[diagnostics][builtins]")
{
    auto diagnostics = collectDiagnostics("exit 0");
    CHECK(diagnostics.empty());
}
