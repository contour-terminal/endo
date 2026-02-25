// SPDX-License-Identifier: Apache-2.0
#include <endo-language/format/FormatConfig.hpp>
#include <endo-language/format/SourceFormatter.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::format;

// ============================================================================
// Comment trivia collection
// ============================================================================

TEST_CASE("Lexer.comment_collection_shell", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("42 # comment"), true);
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    auto const& comments = lexer.comments();
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].style == endo::CommentStyle::Shell);
    CHECK(comments[0].text == "# comment");
}

TEST_CASE("Lexer.comment_collection_cstyle", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("42 // comment"), true);
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    auto const& comments = lexer.comments();
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].style == endo::CommentStyle::CStyle);
    CHECK(comments[0].text == "// comment");
}

TEST_CASE("Lexer.comment_collection_fsharp_block", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("(* block comment *) 42"), true);
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    auto const& comments = lexer.comments();
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].style == endo::CommentStyle::FSharp);
    CHECK(comments[0].text == "(* block comment *)");
}

TEST_CASE("Lexer.comment_collection_disabled_by_default", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("# comment\n42"));
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    CHECK(lexer.comments().empty());
}

// ============================================================================
// FormatConfig
// ============================================================================

TEST_CASE("FormatConfig.defaults", "[format]")
{
    FormatConfig config;
    CHECK(config.indentWidth == 4);
    CHECK(config.useSpaces == true);
    CHECK(config.maxLineWidth == 100);
    CHECK(config.trailingNewline == true);
    CHECK(config.blankLinesBetweenTopLevel == 1);
    CHECK(config.indentString() == "    ");
}

TEST_CASE("FormatConfig.indentString_tabs", "[format]")
{
    FormatConfig config;
    config.useSpaces = false;
    CHECK(config.indentString() == "\t");
}

// ============================================================================
// SourceFormatter — basic formatting
// ============================================================================

TEST_CASE("SourceFormatter.simple_echo", "[format]")
{
    auto const result = SourceFormatter::format("echo hello world");
    CHECK(result == "echo hello world\n");
}

TEST_CASE("SourceFormatter.let_binding_simple", "[format]")
{
    auto const result = SourceFormatter::format("let x = 42");
    CHECK(result == "let x = 42\n");
}

TEST_CASE("SourceFormatter.let_binding_simple_rhs_stays_inline", "[format]")
{
    auto const result = SourceFormatter::format("let x = 2 + 3 * 4");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let x = ") != std::string::npos);
}

TEST_CASE("SourceFormatter.let_binding_compound_rhs_breaks_after_eq", "[format]")
{
    auto const result = SourceFormatter::format("let r = if true then 42 else 0");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let r =\n") != std::string::npos);
    CHECK(result.find("    if true then 42\n") != std::string::npos);
    CHECK(result.find("    else 0\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.let_binding_compound_rhs_idempotency", "[format]")
{
    auto const source = "let r = if true then 42 else 0";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.let_function", "[format]")
{
    auto const result = SourceFormatter::format("let add x y = (x + y)");
    CHECK(result == "let add x y = (x + y)\n");
}

TEST_CASE("SourceFormatter.match_expression", "[format]")
{
    auto const result = SourceFormatter::format("let f x = match x with | 0 -> \"zero\" | _ -> \"other\"");
    // Match arms should be on separate lines
    CHECK(result.find("| 0 ->") != std::string::npos);
    CHECK(result.find("| _ ->") != std::string::npos);
}

TEST_CASE("SourceFormatter.comment_preservation", "[format]")
{
    auto const result = SourceFormatter::format("# this is a comment\nlet x = 42");
    INFO("Formatted result: [" << result << "]");
    CHECK(result.find("# this is a comment") != std::string::npos);
    CHECK(result.find("let x = 42") != std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_between_comments_and_code", "[format]")
{
    auto const result = SourceFormatter::format("# c1\n# c2\n\nlet x = 42");
    INFO("Formatted result: [" << result << "]");
    // The blank line between comments and code should be preserved
    CHECK(result.find("# c2\n\nlet x = 42") != std::string::npos);
}

TEST_CASE("SourceFormatter.trailing_newline", "[format]")
{
    auto const result = SourceFormatter::format("let x = 42");
    CHECK(!result.empty());
    CHECK(result.back() == '\n');
}

TEST_CASE("SourceFormatter.trailing_newline_disabled", "[format]")
{
    FormatConfig config;
    config.trailingNewline = false;
    auto const result = SourceFormatter::format("let x = 42", config);
    CHECK(!result.empty());
    // Should not have trailing newline
    CHECK(result.back() != '\n');
}

TEST_CASE("SourceFormatter.parse_failure_returns_original", "[format]")
{
    auto const source = "let let let ???";
    auto const result = SourceFormatter::format(source);
    CHECK(result == source);
}

// ============================================================================
// Idempotency
// ============================================================================

TEST_CASE("SourceFormatter.idempotency_simple", "[format]")
{
    auto const source = "let x = 42";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.idempotency_match", "[format]")
{
    auto const source = "let f x = match x with | 0 -> \"zero\" | _ -> \"other\"";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.idempotency_if_expr", "[format]")
{
    auto const source = "let f x = if x == 0 then \"zero\" else \"nonzero\"";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    CHECK(first == second);
}

// ============================================================================
// One-expression-per-line rule
// ============================================================================

// --- IfExpr ---

TEST_CASE("SourceFormatter.if_no_else_simple_inline", "[format]")
{
    // No else branch + simple + fits: stays on one line
    auto const result = SourceFormatter::format("let f x = if x == 0 then 42");
    INFO("Result: [" << result << "]");
    CHECK(result.find("if x == 0 then 42") != std::string::npos);
}

TEST_CASE("SourceFormatter.if_else_never_all_on_one_line", "[format]")
{
    // With else: never put all three expressions on one line
    auto const result = SourceFormatter::format("let f x = if x == 0 then 1 else 2");
    INFO("Result: [" << result << "]");
    // Should NOT have `if ... then ... else ...` on one line
    CHECK(result.find("then 1 else 2") == std::string::npos);
    // Should have compact two-line format
    CHECK(result.find("then 1") != std::string::npos);
    CHECK(result.find("else 2") != std::string::npos);
}

TEST_CASE("SourceFormatter.if_else_compact_two_line", "[format]")
{
    auto const result = SourceFormatter::format("let f x = if x > 0 then \"pos\" else \"neg\"");
    INFO("Result: [" << result << "]");
    // Compact two-line: `if cond then thenBody\n    else elseBody`
    CHECK(result.find("then \"pos\"") != std::string::npos);
    CHECK(result.find("else \"neg\"") != std::string::npos);
}

TEST_CASE("SourceFormatter.if_else_compound_then_multiline", "[format]")
{
    // Compound then branch forces multi-line
    auto const result = SourceFormatter::format(
        "let f x = if x > 0 then (match x with | 1 -> \"one\" | _ -> \"other\") else \"neg\"");
    INFO("Result: [" << result << "]");
    // Should use multi-line format with indented branches
    CHECK(result.find("then\n") != std::string::npos);
    CHECK(result.find("else\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.if_else_compound_else_multiline", "[format]")
{
    // Compound else branch forces multi-line (symmetry)
    auto const result = SourceFormatter::format(
        "let f x = if x > 0 then \"pos\" else (match x with | 0 -> \"zero\" | _ -> \"neg\")");
    INFO("Result: [" << result << "]");
    CHECK(result.find("then\n") != std::string::npos);
    CHECK(result.find("else\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.if_else_if_chain", "[format]")
{
    auto const result =
        SourceFormatter::format("let f x = if x > 0 then \"pos\" else if x == 0 then \"zero\" else \"neg\"");
    INFO("Result: [" << result << "]");
    // else if should stay on one line
    CHECK(result.find("else if") != std::string::npos);
}

TEST_CASE("SourceFormatter.if_else_idempotency", "[format]")
{
    auto const source = "let f x = if x == 0 then \"zero\" else \"nonzero\"";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.if_else_compound_idempotency", "[format]")
{
    auto const source = "let f x = if x > 0 then (match x with | 1 -> \"one\" | _ -> \"other\") else \"neg\"";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- MatchExpr ---

TEST_CASE("SourceFormatter.match_simple_arm_inline", "[format]")
{
    // Simple body stays inline
    auto const result = SourceFormatter::format("let f x = match x with | 0 -> \"zero\" | _ -> \"other\"");
    INFO("Result: [" << result << "]");
    CHECK(result.find("| 0 -> \"zero\"") != std::string::npos);
    CHECK(result.find("| _ -> \"other\"") != std::string::npos);
}

TEST_CASE("SourceFormatter.match_compound_arm_body_multiline", "[format]")
{
    // Compound arm body gets indented on next line
    auto const result =
        SourceFormatter::format("let f x = match x with | 0 -> (if true then 1 else 2) | _ -> 3");
    INFO("Result: [" << result << "]");
    // The compound arm body should be indented after `->`
    CHECK(result.find("| 0 ->\n") != std::string::npos);
    CHECK(result.find("| _ -> 3") != std::string::npos);
}

TEST_CASE("SourceFormatter.match_compound_idempotency", "[format]")
{
    auto const source = "let f x = match x with | 0 -> (if true then 1 else 2) | _ -> 3";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- TryWithExpr ---

TEST_CASE("SourceFormatter.try_with_simple_inline", "[format]")
{
    auto const result = SourceFormatter::format("let f x = try x? with | Error e -> 0");
    INFO("Result: [" << result << "]");
    CHECK(result.find("try x? with") != std::string::npos);
    CHECK(result.find("| Error e -> 0") != std::string::npos);
}

TEST_CASE("SourceFormatter.try_with_compound_body_multiline", "[format]")
{
    auto const result =
        SourceFormatter::format("let f x = try (match x with | Some v -> v | None -> 0) with | Error e -> 0");
    INFO("Result: [" << result << "]");
    CHECK(result.find("try\n") != std::string::npos);
    CHECK(result.find("with\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.try_with_compound_handler_multiline", "[format]")
{
    auto const result =
        SourceFormatter::format("let f x = try x? with | Error e -> (if e == 0 then 1 else 2)");
    INFO("Result: [" << result << "]");
    CHECK(result.find("| Error e ->\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.try_with_idempotency", "[format]")
{
    auto const source = "let f x = try (match x with | Some v -> v | None -> 0) with | Error e -> 0";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- TryFinallyExpr ---

TEST_CASE("SourceFormatter.try_finally_simple_compact", "[format]")
{
    auto const result = SourceFormatter::format("let f x = try x finally 0");
    INFO("Result: [" << result << "]");
    CHECK(result.find("try x") != std::string::npos);
    CHECK(result.find("finally 0") != std::string::npos);
    // Should NOT be all on one line
    CHECK(result.find("try x finally 0") == std::string::npos);
}

TEST_CASE("SourceFormatter.try_finally_compound_multiline", "[format]")
{
    auto const result = SourceFormatter::format("let f x = try (if x > 0 then 1 else 2) finally 0");
    INFO("Result: [" << result << "]");
    CHECK(result.find("try\n") != std::string::npos);
    CHECK(result.find("finally\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.try_finally_idempotency", "[format]")
{
    auto const source = "let f x = try x finally 0";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- LambdaExpr ---

TEST_CASE("SourceFormatter.lambda_simple_inline", "[format]")
{
    auto const result = SourceFormatter::format("let f = fun x -> (x + 1)");
    INFO("Result: [" << result << "]");
    CHECK(result.find("fun x -> (x + 1)") != std::string::npos);
}

TEST_CASE("SourceFormatter.lambda_compound_body_multiline", "[format]")
{
    auto const result =
        SourceFormatter::format("let f = fun x -> (match x with | 0 -> \"zero\" | _ -> \"other\")");
    INFO("Result: [" << result << "]");
    CHECK(result.find("fun x ->\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.lambda_idempotency", "[format]")
{
    auto const source = "let f = fun x -> (match x with | 0 -> \"zero\" | _ -> \"other\")";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- LetInExpr ---

TEST_CASE("SourceFormatter.let_in_simple_inline", "[format]")
{
    auto const result = SourceFormatter::format("let f x = let y = 5 in (x + y)");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let y = 5 in (x + y)") != std::string::npos);
}

TEST_CASE("SourceFormatter.let_in_compound_body_multiline", "[format]")
{
    auto const result =
        SourceFormatter::format("let f x = let y = 5 in (match y with | 0 -> \"zero\" | _ -> \"other\")");
    INFO("Result: [" << result << "]");
    // `in` stays on same line as value, body indented on next line
    CHECK(result.find("= 5 in") != std::string::npos);
    CHECK(result.find("in\n") != std::string::npos);
}

TEST_CASE("SourceFormatter.let_in_idempotency", "[format]")
{
    auto const source = "let f x = let y = 5 in (match y with | 0 -> \"zero\" | _ -> \"other\")";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- ForInStmt / WhileStmt leading comments ---

TEST_CASE("SourceFormatter.for_in_preserves_leading_comments", "[format]")
{
    auto const source = "# header comment\n\nfor x in [1; 2] do\n    print x";
    auto const result = SourceFormatter::format(source);
    INFO("Result: [" << result << "]");
    // Comment must appear before the for loop, not inside
    auto const commentPos = result.find("# header comment");
    auto const forPos = result.find("for x in");
    REQUIRE(commentPos != std::string::npos);
    REQUIRE(forPos != std::string::npos);
    CHECK(commentPos < forPos);
}

TEST_CASE("SourceFormatter.for_in_leading_comments_idempotency", "[format]")
{
    auto const source = "# header\n\nfor x in [1; 2] do\n    print x";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.while_preserves_leading_comments", "[format]")
{
    auto const source = "# header comment\n\nwhile true do\n    print 1";
    auto const result = SourceFormatter::format(source);
    INFO("Result: [" << result << "]");
    auto const commentPos = result.find("# header comment");
    auto const whilePos = result.find("while");
    REQUIRE(commentPos != std::string::npos);
    REQUIRE(whilePos != std::string::npos);
    CHECK(commentPos < whilePos);
}

TEST_CASE("SourceFormatter.while_leading_comments_idempotency", "[format]")
{
    auto const source = "# header\n\nwhile true do\n    print 1";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- ForInStmt / WhileStmt dangling body comments ---

TEST_CASE("SourceFormatter.for_in_preserves_body_trailing_comment", "[format]")
{
    auto const source = "for x in [1; 2] do\n    print x\n    # end of body";
    auto const result = SourceFormatter::format(source);
    INFO("Result: [" << result << "]");
    // Comment must appear inside the loop body (indented under do)
    auto const commentPos = result.find("# end of body");
    auto const printPos = result.find("print x");
    REQUIRE(commentPos != std::string::npos);
    REQUIRE(printPos != std::string::npos);
    CHECK(printPos < commentPos);
}

TEST_CASE("SourceFormatter.for_in_body_trailing_comment_idempotency", "[format]")
{
    auto const source = "for x in [1; 2] do\n    print x\n    # end of body";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.while_preserves_body_trailing_comment", "[format]")
{
    auto const source = "while true do\n    print 1\n    # end of body";
    auto const result = SourceFormatter::format(source);
    INFO("Result: [" << result << "]");
    auto const commentPos = result.find("# end of body");
    auto const printPos = result.find("print 1");
    REQUIRE(commentPos != std::string::npos);
    REQUIRE(printPos != std::string::npos);
    CHECK(printPos < commentPos);
}

TEST_CASE("SourceFormatter.while_body_trailing_comment_idempotency", "[format]")
{
    auto const source = "while true do\n    print 1\n    # end of body";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// --- LetBindingStmt leading comments ---

TEST_CASE("SourceFormatter.let_binding_preserves_leading_comments", "[format]")
{
    auto const source = "# header comment\n\nlet xs = 1..5";
    auto const result = SourceFormatter::format(source);
    INFO("Result: [" << result << "]");
    // Comment must appear before the let binding, not after
    auto const commentPos = result.find("# header comment");
    auto const letPos = result.find("let xs");
    REQUIRE(commentPos != std::string::npos);
    REQUIRE(letPos != std::string::npos);
    CHECK(commentPos < letPos);
}

TEST_CASE("SourceFormatter.let_binding_leading_comments_idempotency", "[format]")
{
    auto const source = "# header comment\n\nlet xs = 1..5";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

// ============================================================================
// Blank line grouping — consecutive expressions should not be separated
// ============================================================================

TEST_CASE("SourceFormatter.no_blank_line_between_consecutive_expressions", "[format]")
{
    // Consecutive expression statements (calls) should NOT get blank lines
    auto const result = SourceFormatter::format("print 1\nprint 2\nprint 3");
    INFO("Result: [" << result << "]");
    CHECK(result.find("print 1\nprint 2\nprint 3") != std::string::npos);
}

TEST_CASE("SourceFormatter.no_blank_line_between_shell_commands", "[format]")
{
    // Consecutive shell commands should NOT get blank lines
    auto const result = SourceFormatter::format("echo hello\necho world");
    INFO("Result: [" << result << "]");
    CHECK(result.find("echo hello\necho world") != std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_between_let_and_expression", "[format]")
{
    // A let binding followed by an expression should get a blank line
    auto const result = SourceFormatter::format("let x = 42\nprint x");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let x = 42\n\nprint x") != std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_between_expression_and_let", "[format]")
{
    // An expression followed by a let binding should get a blank line
    auto const result = SourceFormatter::format("print 1\nlet x = 42");
    INFO("Result: [" << result << "]");
    CHECK(result.find("print 1\n\nlet x = 42") != std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_between_consecutive_lets", "[format]")
{
    // Consecutive let bindings should still get blank lines (declarations)
    auto const result = SourceFormatter::format("let x = 1\nlet y = 2");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let x = 1\n\nlet y = 2") != std::string::npos);
}

TEST_CASE("SourceFormatter.consecutive_expressions_idempotency", "[format]")
{
    auto const source = "print 1\nprint 2\nprint 3";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.mixed_expressions_and_lets_idempotency", "[format]")
{
    auto const source = "let x = 1\nprint x\nprint x\nlet y = 2";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.comment_collection_between_lines", "[format]")
{
    // Verify that a comment on its own line is NOT classified as trailing
    auto lexer1 = endo::Lexer(std::make_unique<endo::StringSource>("print 1\n# between\nprint 2"), true);
    while (lexer1.currentToken() != endo::Token::EndOfInput)
        lexer1.nextToken();
    auto const& c1 = lexer1.comments();
    REQUIRE(c1.size() == 1);
    INFO("Comment: text=[" << c1[0].text << "] line=" << c1[0].location.begin.line
                           << " isTrailing=" << c1[0].isTrailing);
    CHECK(c1[0].text == "# between");
    CHECK(c1[0].isTrailing == false);

    auto lexer2 = endo::Lexer(std::make_unique<endo::StringSource>("echo hello\n# middle\necho world"), true);
    while (lexer2.currentToken() != endo::Token::EndOfInput)
        lexer2.nextToken();
    auto const& c2 = lexer2.comments();
    REQUIRE(c2.size() == 1);
    INFO("Comment: text=[" << c2[0].text << "] line=" << c2[0].location.begin.line
                           << " isTrailing=" << c2[0].isTrailing);
    CHECK(c2[0].text == "# middle");
    CHECK(c2[0].isTrailing == false);
}

TEST_CASE("SourceFormatter.comment_between_expressions", "[format]")
{
    // A comment between two expression statements should stay on its own line
    auto const result = SourceFormatter::format("print 1\n# between\nprint 2");
    INFO("Result: [" << result << "]");
    CHECK(result.find("print 1\n# between\nprint 2") != std::string::npos);
}

TEST_CASE("SourceFormatter.comment_between_expressions_idempotency", "[format]")
{
    auto const source = "print 1\n# between\nprint 2";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.comment_between_shell_commands", "[format]")
{
    // A comment between two shell commands should stay on its own line
    auto const result = SourceFormatter::format("echo hello\n# middle\necho world");
    INFO("Result: [" << result << "]");
    CHECK(result.find("echo hello\n# middle\necho world") != std::string::npos);
}

TEST_CASE("SourceFormatter.comment_between_let_and_expression", "[format]")
{
    // A comment between a let binding and an expression should be preserved
    auto const result = SourceFormatter::format("let x = 42\n# comment\nprint x");
    INFO("Result: [" << result << "]");
    CHECK(result.find("# comment") != std::string::npos);
    auto const commentPos = result.find("# comment");
    auto const letPos = result.find("let x = 42");
    auto const printPos = result.find("print x");
    REQUIRE(commentPos != std::string::npos);
    REQUIRE(letPos != std::string::npos);
    REQUIRE(printPos != std::string::npos);
    CHECK(letPos < commentPos);
    CHECK(commentPos < printPos);
}

TEST_CASE("SourceFormatter.comment_preserves_unicode", "[format]")
{
    // Em-dash (U+2014) must survive round-trip through lexer comment collection
    auto const result = SourceFormatter::format("# description: A \xe2\x80\x94 B\nlet x = 42");
    INFO("Result: [" << result << "]");
    CHECK(result.find("A \xe2\x80\x94 B") != std::string::npos);
}

// ============================================================================
// User-authored blank line preservation
// ============================================================================

TEST_CASE("SourceFormatter.preserve_blank_line_between_print_calls", "[format]")
{
    auto const result = SourceFormatter::format("print 1\n\nprint 2");
    INFO("Result: [" << result << "]");
    CHECK(result.find("print 1\n\nprint 2") != std::string::npos);
}

TEST_CASE("SourceFormatter.no_blank_line_when_user_omitted", "[format]")
{
    auto const result = SourceFormatter::format("print 1\nprint 2");
    INFO("Result: [" << result << "]");
    CHECK(result.find("print 1\nprint 2") != std::string::npos);
    // Must NOT have a blank line inserted
    CHECK(result.find("print 1\n\nprint 2") == std::string::npos);
}

TEST_CASE("SourceFormatter.normalize_multiple_blank_lines_to_one", "[format]")
{
    auto const result = SourceFormatter::format("print 1\n\n\n\nprint 2");
    INFO("Result: [" << result << "]");
    // Should have exactly one blank line, not multiple
    CHECK(result.find("print 1\n\nprint 2") != std::string::npos);
    CHECK(result.find("print 1\n\n\nprint 2") == std::string::npos);
}

TEST_CASE("SourceFormatter.preserve_blank_line_between_shell_commands", "[format]")
{
    auto const result = SourceFormatter::format("echo hello\n\necho world");
    INFO("Result: [" << result << "]");
    CHECK(result.find("echo hello\n\necho world") != std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_preserved_at_nested_indent", "[format]")
{
    auto const result = SourceFormatter::format("while true do\n    print 1\n\n    print 2");
    INFO("Result: [" << result << "]");
    // Inside the while body, the blank line should be preserved
    CHECK(result.find("print 1\n\n") != std::string::npos);
    CHECK(result.find("print 2") != std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_preserved_inside_for_loop", "[format]")
{
    auto const result = SourceFormatter::format("for x in [1; 2; 3] do\n    print 1\n\n    print 2");
    INFO("Result: [" << result << "]");
    CHECK(result.find("print 1\n\n") != std::string::npos);
    CHECK(result.find("print 2") != std::string::npos);
}

TEST_CASE("SourceFormatter.declaration_heuristic_still_adds_blank_line", "[format]")
{
    // Even without a user blank line, the declaration heuristic adds one at top level
    auto const result = SourceFormatter::format("let x = 1\nprint x");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let x = 1\n\nprint x") != std::string::npos);
}

TEST_CASE("SourceFormatter.user_blank_line_combined_with_heuristic", "[format]")
{
    // User blank line + declaration heuristic should produce exactly one blank line
    auto const result = SourceFormatter::format("let x = 1\n\nprint x");
    INFO("Result: [" << result << "]");
    CHECK(result.find("let x = 1\n\nprint x") != std::string::npos);
    // Must not double the blank line
    CHECK(result.find("let x = 1\n\n\nprint x") == std::string::npos);
}

TEST_CASE("SourceFormatter.blank_line_preservation_idempotency", "[format]")
{
    auto const source = "print 1\n\nprint 2\nprint 3";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.multiple_blank_lines_normalization_idempotency", "[format]")
{
    auto const source = "print 1\n\n\n\nprint 2";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.nested_blank_line_idempotency", "[format]")
{
    auto const source = "while true do\n    print 1\n\n    print 2";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.blank_line_with_comment_no_duplication", "[format]")
{
    auto const result = SourceFormatter::format("print 1\n\n# comment\nprint 2");
    INFO("Result: [" << result << "]");
    // Should not produce triple newlines
    CHECK(result.find("\n\n\n") == std::string::npos);
    CHECK(result.find("# comment") != std::string::npos);
}

TEST_CASE("SourceFormatter.mixed_blank_and_no_blank_groups", "[format]")
{
    auto const result = SourceFormatter::format("print 1\nprint 2\n\nprint 3\nprint 4");
    INFO("Result: [" << result << "]");
    // No blank line between 1 and 2
    CHECK(result.find("print 1\nprint 2") != std::string::npos);
    // Blank line between 2 and 3
    CHECK(result.find("print 2\n\nprint 3") != std::string::npos);
    // No blank line between 3 and 4
    CHECK(result.find("print 3\nprint 4") != std::string::npos);
}

TEST_CASE("SourceFormatter.mixed_blank_groups_idempotency", "[format]")
{
    auto const source = "print 1\nprint 2\n\nprint 3\nprint 4";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    INFO("First: [" << first << "]");
    INFO("Second: [" << second << "]");
    CHECK(first == second);
}
