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
