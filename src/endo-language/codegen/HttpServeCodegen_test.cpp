// SPDX-License-Identifier: Apache-2.0

#include <endo-language/TestHelper.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

// =================================================================================================
// httpServe — code generation
//
// httpServe is a value-returning builtin whose second argument is a function value, so it cannot
// be expressed by the data-driven builtin table and is emitted by custom codegen (evaluate the
// port, emit a function reference for the handler, call the httpServe(IH)I native callback).
// These tests pin that codegen down: a well-formed call must generate IR, and each misuse must
// produce a clear diagnostic instead of the historical "Undefined function: httpServe".
// The actual serving/wire behaviour is covered by the net-layer round-trip test in
// src/net/HttpServer_test.cpp.
// =================================================================================================

TEST_CASE("httpServe.named_handler_generates_ir", "[fsharp][builtin][net][http]")
{
    // The canonical form: a named string -> string handler passed by identifier.
    CHECK(generatesIRSuccessfully(R"(
let handler path = "you asked for " + path
let rc = httpServe 8080 handler
)"));
}

TEST_CASE("httpServe.bare_statement_generates_ir", "[fsharp][builtin][net][http]")
{
    // httpServe is registered as a statement-level builtin (compilerBuiltins), so the natural
    // bare-statement form parses as an F# application and lowers through the builtin-call codegen
    // — rather than being routed to external-command dispatch (the historical "program not found").
    CHECK(generatesIRSuccessfully(R"(
let handler path = "you asked for " + path
httpServe 8080 handler
)"));
}

TEST_CASE("httpServe.typed_lambda_handler_generates_ir", "[fsharp][builtin][net][http]")
{
    // A parenthesized inline lambda with a typed parameter is also accepted: the paren is
    // unwrapped and the lambda is compiled to a function reference.
    CHECK(generatesIRSuccessfully(R"(
let rc = httpServe 8080 (fun (p: string) -> "hi " + p)
)"));
}

TEST_CASE("httpServe.wrong_arity_is_rejected", "[fsharp][builtin][net][http]")
{
    // One argument is not enough (port and handler are both required).
    CHECK(generatesIRWithError("let rc = httpServe 8080", "httpServe requires exactly 2 arguments"));
}

TEST_CASE("httpServe.non_integer_port_is_rejected", "[fsharp][builtin][net][http]")
{
    CHECK(generatesIRWithError(R"(
let handler path = path
let rc = httpServe "8080" handler
)",
                               "httpServe port argument must be an integer"));
}

TEST_CASE("httpServe.non_function_handler_is_rejected", "[fsharp][builtin][net][http]")
{
    // A non-function second argument (here a plain string) must be reported as such, not as an
    // undefined function or a silent miscompile.
    CHECK(generatesIRWithError(R"(
let rc = httpServe 8080 "not a function"
)",
                               "httpServe handler argument must be a function"));
}
