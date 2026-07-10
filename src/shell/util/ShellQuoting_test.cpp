// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "ShellQuoting.hpp"

using namespace std::string_view_literals;
using endo::escapeDoubleQuoteContext;
using endo::needsShellQuoting;
using endo::quoteCompletionValue;
using endo::shellQuoteDouble;

// =================================================================================================
// needsShellQuoting
// =================================================================================================

TEST_CASE("ShellQuoting.needsShellQuoting.plain_words")
{
    CHECK_FALSE(needsShellQuoting("echo"sv));
    CHECK_FALSE(needsShellQuoting("tool.exe"sv));
    CHECK_FALSE(needsShellQuoting("X:/dir/tool.exe"sv)); // drive-letter, forward slashes: bare-word safe
    CHECK_FALSE(needsShellQuoting("--help"sv));
    CHECK_FALSE(needsShellQuoting("a:b:c"sv)); // colon is deliberately not reserved
}

TEST_CASE("ShellQuoting.needsShellQuoting.empty")
{
    CHECK(needsShellQuoting(""sv)); // empty must be quoted to be a token at all
}

TEST_CASE("ShellQuoting.needsShellQuoting.reserved_characters")
{
    CHECK(needsShellQuoting("a b"sv));           // whitespace
    CHECK(needsShellQuoting("X:/!Programme"sv)); // '!' terminates a bare word
    CHECK(needsShellQuoting("a$b"sv));           // '$' starts interpolation
    CHECK(needsShellQuoting("a|b"sv));           // pipe
    CHECK(needsShellQuoting("a;b"sv));           // statement separator
    CHECK(needsShellQuoting("a(b)"sv));          // parentheses
    CHECK(needsShellQuoting("a<b"sv));           // redirect
    CHECK(needsShellQuoting("a>b"sv));           // redirect
    CHECK(needsShellQuoting("a`b"sv));           // backtick substitution
    CHECK(needsShellQuoting("a\"b"sv));          // embedded double quote
    CHECK(needsShellQuoting("a'b"sv));           // embedded single quote
}

TEST_CASE("ShellQuoting.needsShellQuoting.comment_prefixes")
{
    CHECK(needsShellQuoting("//server/share/tool.exe"sv)); // '//' would start a C-style comment
    CHECK(needsShellQuoting("#file"sv));                   // '#' would start a shell comment
    CHECK_FALSE(needsShellQuoting("a//b"sv));              // '//' only matters at the start
}

// =================================================================================================
// escapeDoubleQuoteContext / shellQuoteDouble
// =================================================================================================

TEST_CASE("ShellQuoting.escapeDoubleQuoteContext.escapes_specials")
{
    CHECK(escapeDoubleQuoteContext("plain"sv) == "plain");
    CHECK(escapeDoubleQuoteContext("a\"b"sv) == "a\\\"b");
    CHECK(escapeDoubleQuoteContext("a\\b"sv) == "a\\\\b");
    CHECK(escapeDoubleQuoteContext("a$b"sv) == "a\\$b");
    CHECK(escapeDoubleQuoteContext("a`b"sv) == "a\\`b");
}

TEST_CASE("ShellQuoting.shellQuoteDouble.wraps_and_escapes")
{
    CHECK(shellQuoteDouble("X:/Program Files/tool.exe"sv) == "\"X:/Program Files/tool.exe\"");
    CHECK(shellQuoteDouble("X:/!Programme/tool.exe"sv) == "\"X:/!Programme/tool.exe\"");
    CHECK(shellQuoteDouble("//server/share/tool.exe"sv) == "\"//server/share/tool.exe\"");
    CHECK(shellQuoteDouble("a$b"sv) == "\"a\\$b\"");
    CHECK(shellQuoteDouble("a\"b"sv) == "\"a\\\"b\"");
}

// =================================================================================================
// quoteCompletionValue — context-aware insertion
// =================================================================================================

TEST_CASE("ShellQuoting.quoteCompletionValue.bare_word")
{
    // Plain value: inserted unchanged.
    CHECK(quoteCompletionValue("tool.exe"sv, '\0') == "tool.exe");
    // Value needing quotes: wrapped and closed (final candidate).
    CHECK(quoteCompletionValue("X:/Program Files/tool.exe"sv, '\0') == "\"X:/Program Files/tool.exe\"");
    CHECK(quoteCompletionValue("X:/!Programme/tool.exe"sv, ' ') == "\"X:/!Programme/tool.exe\"");
}

TEST_CASE("ShellQuoting.quoteCompletionValue.directory_leaves_quote_open")
{
    // Directory candidate (trailing '/'): opening quote, no close, so completion
    // continues inside the same quote on the next Tab.
    CHECK(quoteCompletionValue("X:/Program Files/"sv, '\0') == "\"X:/Program Files/");
    // A directory needing no quoting is inserted as-is.
    CHECK(quoteCompletionValue("X:/dir/"sv, '\0') == "X:/dir/");
}

TEST_CASE("ShellQuoting.quoteCompletionValue.inside_open_double_quote")
{
    // Already inside `"`: escape for the context, do not re-open; close a final candidate.
    CHECK(quoteCompletionValue("X:/Program Files/tool.exe"sv, '"') == "X:/Program Files/tool.exe\"");
    // Directory inside `"`: stays open.
    CHECK(quoteCompletionValue("X:/Program Files/"sv, '"') == "X:/Program Files/");
    // Inner specials are escaped.
    CHECK(quoteCompletionValue("a$b.exe"sv, '"') == "a\\$b.exe\"");
}

TEST_CASE("ShellQuoting.quoteCompletionValue.inside_open_single_quote")
{
    // Single-quoted content is literal: no escaping, close a final candidate.
    CHECK(quoteCompletionValue("X:/Program Files/tool.exe"sv, '\'') == "X:/Program Files/tool.exe'");
    CHECK(quoteCompletionValue("X:/Program Files/"sv, '\'') == "X:/Program Files/");
}
