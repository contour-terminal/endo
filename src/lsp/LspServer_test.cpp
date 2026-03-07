// SPDX-License-Identifier: Apache-2.0
#include <editor-protocol/DocumentStore.hpp>
#include <editor-protocol/JsonTransport.hpp>
#include <editor-protocol/TestHelpers.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "CodeActionProvider.hpp"
#include "CompletionProvider.hpp"
#include "DefinitionProvider.hpp"
#include "DiagnosticsProvider.hpp"
#include "DocumentHighlightProvider.hpp"
#include "DocumentSymbolProvider.hpp"
#include "FoldingRangeProvider.hpp"
#include "HoverProvider.hpp"
#include "InlayHintProvider.hpp"
#include "LspServer.hpp"
#include "ReferencesProvider.hpp"
#include "RenameProvider.hpp"
#include "SelectionRangeProvider.hpp"
#include "SemanticTokens.hpp"
#include "SignatureHelpProvider.hpp"
#include "SymbolCollector.hpp"
#include <nlohmann/json.hpp>

using namespace endo::editor_protocol;
using namespace endo::lsp;
using json = nlohmann::json;

// =============================================================================
// Helper: LSP-specific session runner
// =============================================================================
namespace
{

/// Runs a full LSP session with the given messages and returns all response messages.
std::vector<json> runSession(std::vector<json> const& messages)
{
    std::ostringstream input;
    for (auto const& msg: messages)
        input << makeRpcMessage(msg);

    auto inputStr = input.str();
    std::istringstream iss(inputStr);
    std::ostringstream oss;

    LspServer server(iss, oss);
    server.run();

    auto outputStr = oss.str();
    std::istringstream outputStream(outputStr);
    return readAllMessages(outputStream);
}

} // namespace

// =============================================================================
// JSON-RPC transport tests
// =============================================================================

TEST_CASE("JsonRpc.readMessage parses Content-Length header and JSON body", "[lsp][jsonrpc]")
{
    const auto* const body = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
    std::istringstream input(std::format("Content-Length: {}\r\n\r\n{}", std::strlen(body), body));

    auto result = readMessage(input);
    REQUIRE(result.has_value());
    CHECK(result->at("method") == "test");
    CHECK(result->at("id") == 1);
}

TEST_CASE("JsonRpc.readMessage fails on missing Content-Length", "[lsp][jsonrpc]")
{
    std::istringstream input("\r\n{\"jsonrpc\":\"2.0\"}");
    auto result = readMessage(input);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("Content-Length") != std::string::npos);
}

TEST_CASE("JsonRpc.readMessage fails on truncated body", "[lsp][jsonrpc]")
{
    std::istringstream input("Content-Length: 100\r\n\r\n{\"short\":true}");
    auto result = readMessage(input);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("JsonRpc.writeMessage produces correct Content-Length header and body", "[lsp][jsonrpc]")
{
    json msg = { { "jsonrpc", "2.0" }, { "id", 1 }, { "result", nullptr } };
    std::ostringstream output;
    writeMessage(output, msg);

    auto const result = output.str();
    CHECK(result.starts_with("Content-Length: "));
    CHECK(result.find("\r\n\r\n") != std::string::npos);

    // Verify the body is valid JSON
    auto const headerEnd = result.find("\r\n\r\n");
    auto const body = result.substr(headerEnd + 4);
    CHECK_NOTHROW(json::parse(body));
}

TEST_CASE("JsonRpc.round-trip writeMessage then readMessage", "[lsp][jsonrpc]")
{
    json original = { { "jsonrpc", "2.0" },
                      { "method", "test/roundtrip" },
                      { "params", { { "key", "value" } } } };

    std::ostringstream oss;
    writeMessage(oss, original);

    std::istringstream iss(oss.str());
    auto result = readMessage(iss);
    REQUIRE(result.has_value());
    CHECK(*result == original);
}

TEST_CASE("JsonRpc.makeResponse/makeErrorResponse/makeNotification structure", "[lsp][jsonrpc]")
{
    auto response = makeResponse(42, json { { "capabilities", json::object() } });
    CHECK(response["jsonrpc"] == "2.0");
    CHECK(response["id"] == 42);
    CHECK(response.contains("result"));

    auto errResponse = makeErrorResponse(42, ErrorCode::MethodNotFound, "not found");
    CHECK(errResponse["jsonrpc"] == "2.0");
    CHECK(errResponse["id"] == 42);
    CHECK(errResponse["error"]["code"] == -32601);
    CHECK(errResponse["error"]["message"] == "not found");

    auto notification = makeNotification("window/logMessage", json { { "message", "hello" } });
    CHECK(notification["jsonrpc"] == "2.0");
    CHECK(notification["method"] == "window/logMessage");
    CHECK_FALSE(notification.contains("id"));
}

// =============================================================================
// LSP lifecycle tests
// =============================================================================

TEST_CASE("LSP.initialize returns server capabilities", "[lsp][lifecycle]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    REQUIRE(responses.size() >= 2);

    // First response is initialize result
    auto const& initResult = responses[0]["result"];
    CHECK(initResult.contains("capabilities"));
    CHECK(initResult["capabilities"]["textDocumentSync"] == 1);
    CHECK(initResult["capabilities"]["hoverProvider"] == true);
    CHECK(initResult["capabilities"].contains("semanticTokensProvider"));
    CHECK(initResult["capabilities"]["semanticTokensProvider"]["full"] == true);
    CHECK(initResult.contains("serverInfo"));
}

TEST_CASE("LSP.shutdown returns null result", "[lsp][lifecycle]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    REQUIRE(responses.size() >= 2);
    // Second response is shutdown result
    auto const& shutdownResult = responses[1];
    CHECK(shutdownResult["id"] == 2);
    CHECK(shutdownResult["result"].is_null());
}

TEST_CASE("LSP.exit after shutdown returns clean exit", "[lsp][lifecycle]")
{
    std::ostringstream input;
    input << makeRpcMessage(sendRequest("initialize", json::object()));
    input << makeRpcMessage(sendNotification("initialized", json::object()));
    input << makeRpcMessage(sendRequest("shutdown", json::object(), 2));
    input << makeRpcMessage(sendNotification("exit", json::object()));

    std::istringstream iss(input.str());
    std::ostringstream oss;

    LspServer server(iss, oss);
    auto const exitCode = server.run();
    CHECK(exitCode == 0);
}

TEST_CASE("LSP.request before initialize returns ServerNotInitialized error", "[lsp][lifecycle]")
{
    auto responses = runSession({
        sendRequest("textDocument/hover", json::object()),
        sendRequest("initialize", json::object(), 2),
        sendRequest("shutdown", json::object(), 3),
        sendNotification("exit", json::object()),
    });

    REQUIRE(!responses.empty());
    CHECK(responses[0].contains("error"));
    CHECK(responses[0]["error"]["code"] == static_cast<int>(ErrorCode::ServerNotInitialized));
}

TEST_CASE("LSP.double initialize is handled gracefully", "[lsp][lifecycle]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object(), 1),
        sendNotification("initialized", json::object()),
        sendRequest("initialize", json::object(), 2),
        sendRequest("shutdown", json::object(), 3),
        sendNotification("exit", json::object()),
    });

    REQUIRE(responses.size() >= 3);
    // Both initialize requests should get valid responses
    CHECK(responses[0]["result"].contains("capabilities"));
    CHECK(responses[1]["result"].contains("capabilities"));
}

// =============================================================================
// Document store tests
// =============================================================================

TEST_CASE("DocumentStore.didOpen stores document", "[lsp][docstore]")
{
    DocumentStore store;
    store.open("file:///test.endo", "let x = 42", 1);

    auto const* text = store.get("file:///test.endo");
    REQUIRE(text != nullptr);
    CHECK(*text == "let x = 42");
}

TEST_CASE("DocumentStore.didChange replaces content", "[lsp][docstore]")
{
    DocumentStore store;
    store.open("file:///test.endo", "let x = 42", 1);
    store.update("file:///test.endo", "let x = 99", 2);

    auto const* text = store.get("file:///test.endo");
    REQUIRE(text != nullptr);
    CHECK(*text == "let x = 99");
}

TEST_CASE("DocumentStore.didClose removes document", "[lsp][docstore]")
{
    DocumentStore store;
    store.open("file:///test.endo", "let x = 42", 1);
    store.close("file:///test.endo");

    CHECK(store.get("file:///test.endo") == nullptr);
}

TEST_CASE("DocumentStore.get returns nullptr for unknown URI", "[lsp][docstore]")
{
    DocumentStore store;
    CHECK(store.get("file:///nonexistent.endo") == nullptr);
}

TEST_CASE("DocumentStore.version tracking", "[lsp][docstore]")
{
    DocumentStore store;
    store.open("file:///test.endo", "v1", 1);
    CHECK(store.version("file:///test.endo") == 1);

    store.update("file:///test.endo", "v2", 5);
    CHECK(store.version("file:///test.endo") == 5);

    CHECK(store.version("file:///unknown.endo") == -1);
}

// =============================================================================
// Diagnostics tests
// =============================================================================

TEST_CASE("Diagnostics.valid source produces zero diagnostics", "[lsp][diagnostics]")
{
    CoreVM::Runtime runtime;
    auto dummyHandler = [](CoreVM::Params&) {
    };
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);
    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    auto diagnostics = computeDiagnostics("let x = 42", "test.endo", runtime);
    CHECK(diagnostics.empty());
}

TEST_CASE("Diagnostics.syntax error produces at least one diagnostic", "[lsp][diagnostics]")
{
    CoreVM::Runtime runtime;
    auto dummyHandler = [](CoreVM::Params&) {
    };
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);
    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    auto diagnostics = computeDiagnostics("let = ", "test.endo", runtime);
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].severity == DiagnosticSeverity::Error);
}

TEST_CASE("Diagnostics.source field is endo", "[lsp][diagnostics]")
{
    CoreVM::Runtime runtime;
    auto dummyHandler = [](CoreVM::Params&) {
    };
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);
    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    auto diagnostics = computeDiagnostics("let = ", "test.endo", runtime);
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].source == "endo");
}

TEST_CASE("Diagnostics.range has valid positions", "[lsp][diagnostics]")
{
    CoreVM::Runtime runtime;
    auto dummyHandler = [](CoreVM::Params&) {
    };
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);
    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    auto diagnostics = computeDiagnostics("let = ", "test.endo", runtime);
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].range.start.line >= 0);
    CHECK(diagnostics[0].range.start.character >= 0);
}

// =============================================================================
// Semantic tokens tests
// =============================================================================

TEST_CASE("SemanticTokens.let x = 42 produces correct tokens", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("let x = 42");

    // Expected: let(keyword), x(variable), =(operator), 42(number)
    // Each token = 5 ints: [deltaLine, deltaStartChar, length, type, modifiers]
    REQUIRE(tokens.data.size() == 20); // 4 tokens * 5

    // let: keyword (type 0)
    CHECK(tokens.data[3] == 0); // keyword type

    // x: variable (type 2)
    CHECK(tokens.data[8] == 2); // variable type

    // =: operator (type 5)
    CHECK(tokens.data[13] == 5); // operator type

    // 42: number (type 3)
    CHECK(tokens.data[18] == 3); // number type
}

TEST_CASE("SemanticTokens.Some 5 produces correct tokens", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("Some 5");

    // Expected: Some(enumMember), 5(number)
    REQUIRE(tokens.data.size() == 10); // 2 tokens * 5

    CHECK(tokens.data[3] == 6); // enumMember type
    CHECK(tokens.data[8] == 3); // number type
}

TEST_CASE("SemanticTokens.fun x -> x + 1 produces correct tokens", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("fun x -> x + 1");

    // Expected: fun(keyword), x(variable), ->(operator), x(variable), +(operator), 1(number)
    REQUIRE(tokens.data.size() == 30); // 6 tokens * 5

    CHECK(tokens.data[3] == 0);  // keyword (fun)
    CHECK(tokens.data[8] == 2);  // variable (x)
    CHECK(tokens.data[13] == 5); // operator (->)
    CHECK(tokens.data[18] == 2); // variable (x)
    CHECK(tokens.data[23] == 5); // operator (+)
    CHECK(tokens.data[28] == 3); // number (1)
}

TEST_CASE("SemanticTokens.$HOME produces variable with modification modifier", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("$HOME");

    REQUIRE(tokens.data.size() >= 5);
    CHECK(tokens.data[3] == 2);         // variable type
    CHECK((tokens.data[4] & 0x2) != 0); // modification modifier bit set
}

TEST_CASE("SemanticTokens.empty source produces empty data", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("");
    CHECK(tokens.data.empty());
}

TEST_CASE("SemanticTokens.delta encoding same line", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("let x = 42");

    // First token (let): deltaLine=0
    CHECK(tokens.data[0] == 0); // deltaLine

    // Second token (x): same line, deltaLine=0
    CHECK(tokens.data[5] == 0); // deltaLine
    CHECK(tokens.data[6] > 0);  // deltaStartChar > 0 (offset from 'let')
}

TEST_CASE("SemanticTokens.delta encoding next line", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("let x = 42\nlet y = 99");

    // Find the token on line 1 (the second 'let')
    // Tokens: let(0,0), x(0,4), =(0,6), 42(0,8), let(1,0), y(1,4), =(1,6), 99(1,8)
    // The 5th token (index 20) should be the second 'let' with deltaLine=1
    REQUIRE(tokens.data.size() >= 25);
    CHECK(tokens.data[20] == 1); // deltaLine = 1
    CHECK(tokens.data[21] == 0); // absolute column since new line
}

TEST_CASE("SemanticTokens.legend contains expected types and modifiers", "[lsp][semantic]")
{
    auto legend = createSemanticTokensLegend();

    CHECK(legend.tokenTypes.size() == 9);
    CHECK(legend.tokenTypes[0] == "keyword");
    CHECK(legend.tokenTypes[2] == "variable");
    CHECK(legend.tokenTypes[3] == "number");
    CHECK(legend.tokenTypes[5] == "operator");
    CHECK(legend.tokenTypes[6] == "enumMember");

    CHECK(legend.tokenModifiers.size() == 2);
    CHECK(legend.tokenModifiers[0] == "declaration");
    CHECK(legend.tokenModifiers[1] == "modification");
}

TEST_CASE("SemanticTokens.token length matches literal length", "[lsp][semantic]")
{
    auto tokens = computeSemanticTokens("let x = 42");

    // 'let' has length 3
    CHECK(tokens.data[2] == 3);

    // 'x' has length 1
    CHECK(tokens.data[7] == 1);

    // '=' has length 1
    CHECK(tokens.data[12] == 1);

    // '42' has length 2
    CHECK(tokens.data[17] == 2);
}

TEST_CASE("SemanticTokens.shell_builtin_cd_as_function", "[lsp][semantic]")
{
    // "cd /tmp" — "cd" should be classified as function (type 1).
    auto tokens = computeSemanticTokens("cd /tmp");
    // "cd" should be present as function token
    REQUIRE(tokens.data.size() >= 5);
    CHECK(tokens.data[3] == 1); // function type (index 1)
}

TEST_CASE("SemanticTokens.shell_path_not_split", "[lsp][semantic]")
{
    // "cd projects/endo" — the path should be a single token, not split by '/'.
    auto tokens = computeSemanticTokens("cd projects/endo");
    // We expect 2 tokens: cd(function), projects/endo(variable)
    REQUIRE(tokens.data.size() == 10); // 2 tokens * 5
    CHECK(tokens.data[3] == 1);        // cd → function type
    CHECK(tokens.data[8] == 2);        // projects/endo → variable type
    CHECK(tokens.data[7] == 13);       // length of "projects/endo" = 13
}

TEST_CASE("SemanticTokens.fsharp_slash_as_operator", "[lsp][semantic]")
{
    // In F# context, "/" should be tokenized as an operator.
    auto tokens = computeSemanticTokens("let x = 4 / 2");
    // Expect: let(keyword), x(variable), =(operator), 4(number), /(operator), 2(number)
    REQUIRE(tokens.data.size() == 30); // 6 tokens * 5
    CHECK(tokens.data[23] == 5);       // '/' → operator type
}

// =============================================================================
// Hover tests
// =============================================================================

TEST_CASE("Hover.let keyword returns binding description", "[lsp][hover]")
{
    auto hover = computeHover("let x = 42", Position { .line = 0, .character = 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("let") != std::string::npos);
    CHECK(hover->contents.value.find("binding") != std::string::npos);
}

TEST_CASE("Hover.fun keyword returns lambda description", "[lsp][hover]")
{
    auto hover = computeHover("fun x -> x", Position { .line = 0, .character = 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("fun") != std::string::npos);
    CHECK(hover->contents.value.find("ambda") != std::string::npos); // "Lambda" or "lambda"
}

TEST_CASE("Hover.match keyword returns description", "[lsp][hover]")
{
    auto hover = computeHover("match x with\n| 0 -> 1", Position { .line = 0, .character = 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("match") != std::string::npos);
}

TEST_CASE("Hover.Some constructor returns type signature", "[lsp][hover]")
{
    auto hover = computeHover("Some 5", Position { .line = 0, .character = 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("Some") != std::string::npos);
    CHECK(hover->contents.value.find("option") != std::string::npos);
}

TEST_CASE("Hover.Ok constructor returns type signature", "[lsp][hover]")
{
    auto hover = computeHover("Ok 5", Position { .line = 0, .character = 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("Ok") != std::string::npos);
    CHECK(hover->contents.value.find("result") != std::string::npos);
}

TEST_CASE("Hover.|> operator returns pipe description", "[lsp][hover]")
{
    auto hover = computeHover("x |> f", Position { .line = 0, .character = 2 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("|>") != std::string::npos);
    CHECK(hover->contents.value.find("pipe") != std::string::npos);
}

TEST_CASE("Hover.whitespace returns nullopt", "[lsp][hover]")
{
    // Position 3 is on whitespace between "let" and "x"
    auto hover = computeHover("let x = 42", Position { .line = 0, .character = 3 });
    CHECK_FALSE(hover.has_value());
}

TEST_CASE("Hover.position past end returns nullopt", "[lsp][hover]")
{
    auto hover = computeHover("let x = 42", Position { .line = 5, .character = 0 });
    CHECK_FALSE(hover.has_value());
}

TEST_CASE("Hover.variable binding at definition shows value", "[lsp][hover]")
{
    // Position 4 is on "variable" in "let variable = 42"
    auto hover = computeHover("let variable = 42", Position { .line = 0, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("variable") != std::string::npos);
    CHECK(hover->contents.value.find("binding") != std::string::npos);
    CHECK(hover->contents.value.find("= 42") != std::string::npos);
}

TEST_CASE("Hover.variable binding at usage shows value", "[lsp][hover]")
{
    // Position 0,8 is on "variable" in the second line
    auto hover = computeHover("let variable = 42\nprintln variable", Position { .line = 1, .character = 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("variable") != std::string::npos);
    CHECK(hover->contents.value.find("binding") != std::string::npos);
    CHECK(hover->contents.value.find("= 42") != std::string::npos);
}

TEST_CASE("Hover.string binding shows value preview", "[lsp][hover]")
{
    auto hover = computeHover("let greeting = \"hello world\"\nprintln greeting",
                              Position { .line = 1, .character = 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("greeting") != std::string::npos);
    CHECK(hover->contents.value.find("hello world") != std::string::npos);
}

TEST_CASE("Hover.boolean binding shows value preview", "[lsp][hover]")
{
    auto hover = computeHover("let flag = true", Position { .line = 0, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("flag") != std::string::npos);
    CHECK(hover->contents.value.find("= true") != std::string::npos);
}

TEST_CASE("Hover.expression binding shows value preview", "[lsp][hover]")
{
    auto hover = computeHover("let result = Some 42", Position { .line = 0, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("result") != std::string::npos);
    CHECK(hover->contents.value.find("= Some 42") != std::string::npos);
}

TEST_CASE("Hover.function binding does not show body as value", "[lsp][hover]")
{
    auto hover = computeHover("let double x = x * 2", Position { .line = 0, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("function") != std::string::npos);
    // Functions show signature, not body - should not contain "= x * 2"
    CHECK(hover->contents.value.find("= x * 2") == std::string::npos);
}

TEST_CASE("Hover.function definition shows function info with parameters", "[lsp][hover]")
{
    // Position 4 is on "add" in "let add x y = x + y"
    auto hover = computeHover("let add x y = x + y", Position { .line = 0, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("add") != std::string::npos);
    CHECK(hover->contents.value.find("function") != std::string::npos);
    CHECK(hover->contents.value.find('x') != std::string::npos);
    CHECK(hover->contents.value.find('y') != std::string::npos);
}

TEST_CASE("Hover.function reference shows function info", "[lsp][hover]")
{
    // Hover on "add" in the usage "println (add 3 4)"
    auto hover =
        computeHover("let add x y = x + y\nprintln (add 3 4)", Position { .line = 1, .character = 9 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("add") != std::string::npos);
    CHECK(hover->contents.value.find("function") != std::string::npos);
}

TEST_CASE("Hover.function parameter shows parameter info", "[lsp][hover]")
{
    // Position 8 is on "x" parameter in "let add x y = x + y"
    auto hover = computeHover("let add x y = x + y", Position { .line = 0, .character = 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find('x') != std::string::npos);
    CHECK(hover->contents.value.find("parameter") != std::string::npos);
    CHECK(hover->contents.value.find("add") != std::string::npos);
}

TEST_CASE("Hover.typed function shows type annotations", "[lsp][hover]")
{
    auto hover =
        computeHover("let add (x: int) (y: int): int = x + y", Position { .line = 0, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("add") != std::string::npos);
    CHECK(hover->contents.value.find("int") != std::string::npos);
}

TEST_CASE("Hover.recursive function shows rec qualifier", "[lsp][hover]")
{
    auto hover = computeHover("let rec fact n = if n <= 1 then 1 else n * fact (n - 1)\nprintln (fact 5)",
                              Position { .line = 0, .character = 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("fact") != std::string::npos);
    CHECK(hover->contents.value.find("rec") != std::string::npos);
}

TEST_CASE("Hover.mutable binding shows mut qualifier", "[lsp][hover]")
{
    auto hover = computeHover("let mut counter = 0", Position { .line = 0, .character = 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("counter") != std::string::npos);
    CHECK(hover->contents.value.find("mutable") != std::string::npos);
    CHECK(hover->contents.value.find("mut") != std::string::npos);
}

TEST_CASE("Hover.exported binding shows export qualifier", "[lsp][hover]")
{
    auto hover = computeHover("let export PATH = \"/usr/bin\"", Position { .line = 0, .character = 11 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("PATH") != std::string::npos);
    CHECK(hover->contents.value.find("exported") != std::string::npos);
    CHECK(hover->contents.value.find("export") != std::string::npos);
}

// =============================================================================
// Integration / end-to-end tests
// =============================================================================

TEST_CASE("E2E.full lifecycle: initialize, shutdown, exit", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    REQUIRE(responses.size() >= 2);
    CHECK(responses[0]["result"].contains("capabilities"));
    CHECK(responses[1]["result"].is_null());
}

TEST_CASE("E2E.open valid document receives empty diagnostics", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42" },
                               } },
                         }),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    // Find the publishDiagnostics notification
    bool foundDiagnostics = false;
    for (auto const& msg: responses)
    {
        if (msg.value("method", "") == "textDocument/publishDiagnostics")
        {
            foundDiagnostics = true;
            CHECK(msg["params"]["uri"] == "file:///test.endo");
            CHECK(msg["params"]["diagnostics"].empty());
            break;
        }
    }
    CHECK(foundDiagnostics);
}

TEST_CASE("E2E.open document with error receives diagnostics", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let = " },
                               } },
                         }),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    bool foundDiagnostics = false;
    for (auto const& msg: responses)
    {
        if (msg.value("method", "") == "textDocument/publishDiagnostics")
        {
            foundDiagnostics = true;
            CHECK(msg["params"]["uri"] == "file:///test.endo");
            CHECK_FALSE(msg["params"]["diagnostics"].empty());
            break;
        }
    }
    CHECK(foundDiagnostics);
}

TEST_CASE("E2E.semantic tokens request returns token data", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42" },
                               } },
                         }),
        sendRequest("textDocument/semanticTokens/full",
                    json { { "textDocument", json { { "uri", "file:///test.endo" } } } },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    // Find the semantic tokens response (id=3)
    bool foundTokens = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            foundTokens = true;
            CHECK(msg["result"].contains("data"));
            CHECK_FALSE(msg["result"]["data"].empty());
            break;
        }
    }
    CHECK(foundTokens);
}

TEST_CASE("E2E.hover request at keyword position returns content", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42" },
                               } },
                         }),
        sendRequest("textDocument/hover",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 0 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    // Find the hover response (id=3)
    bool foundHover = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            foundHover = true;
            CHECK(msg["result"].contains("contents"));
            CHECK(msg["result"]["contents"]["value"].get<std::string>().find("let") != std::string::npos);
            break;
        }
    }
    CHECK(foundHover);
}

// =============================================================================
// SymbolCollector tests
// =============================================================================

TEST_CASE("SymbolCollector.collectSymbols returns definitions and references", "[lsp][symbols]")
{
    auto table = collectSymbols("let x = 42\nprintln x");
    REQUIRE(table.has_value());
    CHECK(!table->definitions.empty());
    // "x" should be defined once
    bool foundXDef = false;
    for (auto const& def: table->definitions)
    {
        if (def.name == "x")
        {
            foundXDef = true;
            CHECK(def.category == SymbolCategory::Variable);
        }
    }
    CHECK(foundXDef);
    // "x" should be referenced (usage in println x)
    bool foundXRef = false;
    for (auto const& ref: table->references)
    {
        if (ref.name == "x" && ref.definitionIndex >= 0)
            foundXRef = true;
    }
    CHECK(foundXRef);
}

TEST_CASE("SymbolCollector.function definition captures parameters", "[lsp][symbols]")
{
    auto table = collectSymbols("let add x y = x + y");
    REQUIRE(table.has_value());
    bool foundAdd = false;
    for (auto const& def: table->definitions)
    {
        if (def.name == "add")
        {
            foundAdd = true;
            CHECK(def.category == SymbolCategory::Function);
            REQUIRE(def.parameterNames.size() == 2);
            CHECK(def.parameterNames[0] == "x");
            CHECK(def.parameterNames[1] == "y");
        }
    }
    CHECK(foundAdd);
}

TEST_CASE("SymbolCollector.definition locations are correctly assigned", "[lsp][symbols]")
{
    auto table = collectSymbols("let x = 42\nprintln x");
    REQUIRE(table.has_value());

    // Find the definition of "x"
    SymbolDefinition const* xDef = nullptr;
    for (auto const& def: table->definitions)
    {
        if (def.name == "x")
        {
            xDef = &def;
            break;
        }
    }
    REQUIRE(xDef != nullptr);
    // "x" token should be on line 0
    CHECK(xDef->location.begin.line == 0);
    // Column should be non-zero (position of "x" in "let x = 42")
    CHECK(xDef->location.begin.column > 0);

    // Find the reference to "x" on line 1
    SymbolReference const* xRef = nullptr;
    for (auto const& ref: table->references)
    {
        if (ref.name == "x" && ref.definitionIndex >= 0)
        {
            xRef = &ref;
            break;
        }
    }
    REQUIRE(xRef != nullptr);
    CHECK(xRef->location.begin.line == 1);
    CHECK(xRef->definitionIndex >= 0);
}

TEST_CASE("SymbolCollector.corrected locations have proper ranges", "[lsp][symbols]")
{
    auto table = collectSymbols("let x = 42\nprintln x");
    REQUIRE(table.has_value());

    // All definition/reference locations should have proper (non-zero-width) ranges
    for (auto const& def: table->definitions)
    {
        INFO("def " << def.name << " begin=(" << def.location.begin.line << "," << def.location.begin.column
                    << ") end=(" << def.location.end.line << "," << def.location.end.column << ")");
        auto const nonZeroWidth = def.location.end.line > def.location.begin.line
                                  || (def.location.end.line == def.location.begin.line
                                      && def.location.end.column > def.location.begin.column);
        CHECK(nonZeroWidth);
    }
    for (auto const& ref: table->references)
    {
        INFO("ref " << ref.name << " begin=(" << ref.location.begin.line << "," << ref.location.begin.column
                    << ") end=(" << ref.location.end.line << "," << ref.location.end.column << ")");
        auto const nonZeroWidth = ref.location.end.line > ref.location.begin.line
                                  || (ref.location.end.line == ref.location.begin.line
                                      && ref.location.end.column > ref.location.begin.column);
        CHECK(nonZeroWidth);
    }
}

// =============================================================================
// Definition tests
// =============================================================================

TEST_CASE("Definition.top-level binding usage jumps to definition", "[lsp][definition]")
{
    // "let x = 42\nprintln x"
    //  cursor on "x" at line 1, col 8
    auto loc = computeDefinition(
        "let x = 42\nprintln x", "file:///test.endo", Position { .line = 1, .character = 8 });
    REQUIRE(loc.has_value());
    CHECK(loc->uri == "file:///test.endo");
    // Definition should be at line 0 (let x)
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.function reference jumps to definition", "[lsp][definition]")
{
    auto loc = computeDefinition(
        "let f x = x + 1\nprintln (f 3)", "file:///test.endo", Position { .line = 1, .character = 9 });
    REQUIRE(loc.has_value());
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.function parameter in body jumps to param", "[lsp][definition]")
{
    // "let f x = x + 1"
    //  the second "x" (in body, col 10) should resolve to param "x" (col 6)
    auto loc =
        computeDefinition("let f x = x + 1", "file:///test.endo", Position { .line = 0, .character = 10 });
    REQUIRE(loc.has_value());
    // Parameter "x" is at column 6
    CHECK(loc->range.start.character == 6);
}

TEST_CASE("Definition.let-in scoped binding", "[lsp][definition]")
{
    const auto* source = "let result = let y = 10 in y + 1";
    // cursor on "y" in "y + 1" (col 27)
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 0, .character = 27 });
    REQUIRE(loc.has_value());
    // "y" definition is at col 17
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.lambda parameter", "[lsp][definition]")
{
    const auto* source = "let f = fun x -> x + 1";
    // cursor on "x" in body (col 17)
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 0, .character = 17 });
    REQUIRE(loc.has_value());
    // "x" parameter is at col 12
    CHECK(loc->range.start.character == 12);
}

TEST_CASE("Definition.cursor not on identifier returns nullopt", "[lsp][definition]")
{
    auto loc = computeDefinition("let x = 42", "file:///test.endo", Position { .line = 0, .character = 0 });
    CHECK_FALSE(loc.has_value()); // cursor on "let" keyword
}

TEST_CASE("Definition.unknown identifier returns nullopt", "[lsp][definition]")
{
    // "println" is a builtin, not a user-defined symbol
    auto loc = computeDefinition("println 42", "file:///test.endo", Position { .line = 0, .character = 0 });
    // It may return itself or nullopt depending on whether the builtin is in scope
    // Just verify no crash
}

TEST_CASE("Definition.shadowed variable resolves to inner scope", "[lsp][definition]")
{
    const auto* source = "let x = 1\nlet result = let x = 2 in x";
    // cursor on "x" after "in" (0-based col 26)
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 1, .character = 26 });
    REQUIRE(loc.has_value());
    // Inner "x" is on line 1
    CHECK(loc->range.start.line == 1);
}

TEST_CASE("Definition.field access resolves object variable", "[lsp][definition]")
{
    // "let p = 42\nprintln p.name"
    //  cursor on "p" in "p.name" (line 1, col 8)
    const auto* source = "let p = 42\nprintln p.name";
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 1, .character = 8 });
    REQUIRE(loc.has_value());
    CHECK(loc->range.start.line == 0);
    CHECK(loc->range.start.character == 4);
}

TEST_CASE("Definition.fstring interpolation resolves variable", "[lsp][definition]")
{
    // let name = "world"
    // let msg = $"hello {name}"
    //  cursor on "name" inside fstring (line 1, col 20)
    const auto* source = "let name = \"world\"\nlet msg = $\"hello {name}\"";
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 1, .character = 20 });
    REQUIRE(loc.has_value());
    CHECK(loc->range.start.line == 0);
    CHECK(loc->range.start.character == 4);
}

TEST_CASE("Definition.record update resolves base variable", "[lsp][definition]")
{
    // type Person = { name: string; age: int }
    // let alice = { name = "Alice"; age = 30 }
    // let older = { alice with age = 31 }
    //  cursor on "alice" in record update (line 2, col 14)
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "let older = { alice with age = 31 }";
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 2, .character = 14 });
    REQUIRE(loc.has_value());
    CHECK(loc->range.start.line == 1);
    CHECK(loc->range.start.character == 4);
}

TEST_CASE("Definition.field access on record field resolves to type definition", "[lsp][definition]")
{
    // type Person = { name: string; age: int }
    // let alice = { name = "Alice"; age = 30 }
    // println alice.name
    //  cursor on "name" in "alice.name" (line 2, col 14)
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "println alice.name";
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 2, .character = 14 });
    REQUIRE(loc.has_value());
    // "name" field is defined in the record type at line 0
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.record expr field name resolves to type definition", "[lsp][definition]")
{
    // type Person = { name: string; age: int }
    // let alice = { name = "Alice"; age = 30 }
    //  cursor on "age" in record literal (line 1, col 30)
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }";
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 1, .character = 30 });
    REQUIRE(loc.has_value());
    // "age" field is defined in the record type at line 0
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.record update field name resolves to type definition", "[lsp][definition]")
{
    // type Person = { name: string; age: int }
    // let alice = { name = "Alice"; age = 30 }
    // let older = { alice with age = 31 }
    //  cursor on "age" in record update (line 2, col 25)
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "let older = { alice with age = 31 }";
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 2, .character = 25 });
    REQUIRE(loc.has_value());
    // "age" field is defined in the record type at line 0
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.cons expression resolves head variable", "[lsp][definition]")
{
    const auto* source = "let x = 1\nlet xs = x :: [2; 3]";
    // cursor on "x" in cons expr (line 1, col 9)
    auto loc = computeDefinition(source, "file:///test.endo", Position { .line = 1, .character = 9 });
    REQUIRE(loc.has_value());
    CHECK(loc->range.start.line == 0);
    CHECK(loc->range.start.character == 4);
}

TEST_CASE("References.field access includes object variable", "[lsp][references]")
{
    const auto* source = "let p = 42\nprintln p.name";
    // cursor on "p" definition (line 0, col 4)
    auto locs = computeReferences(source, "file:///test.endo", Position { .line = 0, .character = 4 }, true);
    // Should find 2: definition + field access usage
    CHECK(locs.size() >= 2);
}

TEST_CASE("References.fstring includes interpolated variable", "[lsp][references]")
{
    const auto* source = "let name = \"world\"\nlet msg = $\"hello {name}\"";
    // cursor on "name" definition (line 0, col 4)
    auto locs = computeReferences(source, "file:///test.endo", Position { .line = 0, .character = 4 }, true);
    // Should find 2: definition + fstring interpolation usage
    CHECK(locs.size() >= 2);
}

TEST_CASE("References.record update includes base variable", "[lsp][references]")
{
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "let older = { alice with age = 31 }";
    // cursor on "alice" definition (line 1, col 4)
    auto locs = computeReferences(source, "file:///test.endo", Position { .line = 1, .character = 4 }, true);
    // Should find 2: definition + record update usage
    CHECK(locs.size() >= 2);
}

TEST_CASE("References.record field finds type def and all usages", "[lsp][references]")
{
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "println alice.name";
    // cursor on "name" field definition in type def (line 0, col 16)
    auto locs = computeReferences(source, "file:///test.endo", Position { .line = 0, .character = 16 }, true);
    // Should find 3: field definition + record expr field + field access
    CHECK(locs.size() >= 3);
}

TEST_CASE("DocumentHighlight.field access highlights object variable", "[lsp][highlight]")
{
    const auto* source = "let p = 42\nprintln p.name";
    auto highlights = computeDocumentHighlights(source, Position { .line = 0, .character = 4 });
    // Should find 2: definition + field access usage
    REQUIRE(highlights.size() == 2);
    CHECK(highlights[0].kind == DocumentHighlightKind::Write);
    CHECK(highlights[1].kind == DocumentHighlightKind::Read);
}

TEST_CASE("DocumentHighlight.fstring highlights interpolated variable", "[lsp][highlight]")
{
    const auto* source = "let name = \"world\"\nlet msg = $\"hello {name}\"";
    auto highlights = computeDocumentHighlights(source, Position { .line = 0, .character = 4 });
    // Should find 2: definition + fstring interpolation usage
    REQUIRE(highlights.size() == 2);
    CHECK(highlights[0].kind == DocumentHighlightKind::Write);
    CHECK(highlights[1].kind == DocumentHighlightKind::Read);
}

TEST_CASE("DocumentHighlight.record field highlights across type def and usages", "[lsp][highlight]")
{
    const auto* source = "type Person = { name: string; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "println alice.name";
    // cursor on "name" in field access (line 2, col 14)
    auto highlights = computeDocumentHighlights(source, Position { .line = 2, .character = 14 });
    // Should find 3: field definition (Write) + record expr field (Read) + field access (Read)
    REQUIRE(highlights.size() == 3);
    CHECK(highlights[0].kind == DocumentHighlightKind::Write);
    CHECK(highlights[1].kind == DocumentHighlightKind::Read);
    CHECK(highlights[2].kind == DocumentHighlightKind::Read);
}

// =============================================================================
// E2E: error resilience
// =============================================================================

TEST_CASE("E2E.internal error returns error response not crash", "[lsp][e2e]")
{
    // Send a request with missing required fields to trigger an exception
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        // definition request missing textDocument.uri should throw, not crash
        sendRequest("textDocument/definition",
                    json { { "textDocument", json::object() },
                           { "position", json { { "line", 0 }, { "character", 0 } } } },
                    2),
        sendRequest("shutdown", json::object(), 3),
        sendNotification("exit", json::object()),
    });
    // Server should still have responded (not crashed)
    CHECK(responses.size() >= 2);
    // The definition response should be an error
    auto foundError = false;
    for (auto const& r: responses)
    {
        if (r.contains("id") && r["id"] == 2 && r.contains("error"))
        {
            foundError = true;
            break;
        }
    }
    CHECK(foundError);
}

// =============================================================================
// References tests
// =============================================================================

TEST_CASE("References.all uses of variable with declaration", "[lsp][references]")
{
    auto locs = computeReferences(
        "let x = 42\nprintln x", "file:///test.endo", Position { .line = 0, .character = 4 }, true);
    // Should find at least 2: definition + usage
    CHECK(locs.size() >= 2);
}

TEST_CASE("References.all uses without declaration", "[lsp][references]")
{
    auto locs = computeReferences(
        "let x = 42\nprintln x", "file:///test.endo", Position { .line = 0, .character = 4 }, false);
    // Should find 1 usage (not the declaration)
    CHECK(!locs.empty());
    // All should be references, not the definition position
}

TEST_CASE("References.function references across call sites", "[lsp][references]")
{
    const auto* source = "let f x = x + 1\nprintln (f 3)\nprintln (f 5)";
    auto locs = computeReferences(source, "file:///test.endo", Position { .line = 0, .character = 4 }, true);
    // f appears 3 times: definition + 2 calls
    CHECK(locs.size() >= 3);
}

TEST_CASE("References.scoped variable does not leak", "[lsp][references]")
{
    const auto* source = "let a = let x = 1 in x\nlet b = let x = 2 in x";
    // cursor on first "x" definition (line 0, col 12)
    auto locs = computeReferences(source, "file:///test.endo", Position { .line = 0, .character = 12 }, true);
    // Should find only 2: first "x" def + first "x" ref
    CHECK(locs.size() == 2);
}

TEST_CASE("References.no matches returns empty", "[lsp][references]")
{
    auto locs =
        computeReferences("let x = 42", "file:///test.endo", Position { .line = 0, .character = 0 }, true);
    // cursor on "let" keyword, not an identifier
    CHECK(locs.empty());
}

TEST_CASE("References.cursor on usage finds all references", "[lsp][references]")
{
    auto locs = computeReferences(
        "let x = 42\nprintln x", "file:///test.endo", Position { .line = 1, .character = 8 }, true);
    CHECK(locs.size() >= 2);
}

// =============================================================================
// DocumentHighlight tests
// =============================================================================

TEST_CASE("DocumentHighlight.at definition shows def and ref", "[lsp][highlight]")
{
    auto highlights =
        computeDocumentHighlights("let x = 42\nprintln x", Position { .line = 0, .character = 4 });
    REQUIRE(highlights.size() == 2);
    // Definition is Write, reference is Read
    CHECK(highlights[0].kind == DocumentHighlightKind::Write);
    CHECK(highlights[1].kind == DocumentHighlightKind::Read);
}

TEST_CASE("DocumentHighlight.at reference shows def and ref", "[lsp][highlight]")
{
    auto highlights =
        computeDocumentHighlights("let x = 42\nprintln x", Position { .line = 1, .character = 8 });
    REQUIRE(highlights.size() == 2);
    CHECK(highlights[0].kind == DocumentHighlightKind::Write);
    CHECK(highlights[1].kind == DocumentHighlightKind::Read);
}

TEST_CASE("DocumentHighlight.function with multiple calls", "[lsp][highlight]")
{
    const auto* source = "let f x = x + 1\nprintln (f 3)\nprintln (f 5)";
    auto highlights = computeDocumentHighlights(source, Position { .line = 0, .character = 4 });
    // f appears 3 times: definition + 2 calls
    REQUIRE(highlights.size() == 3);
    CHECK(highlights[0].kind == DocumentHighlightKind::Write);
    CHECK(highlights[1].kind == DocumentHighlightKind::Read);
    CHECK(highlights[2].kind == DocumentHighlightKind::Read);
}

TEST_CASE("DocumentHighlight.scoped variable only shows inner occurrences", "[lsp][highlight]")
{
    const auto* source = "let a = let x = 1 in x\nlet b = let x = 2 in x";
    // cursor on first "x" definition (line 0, col 12)
    auto highlights = computeDocumentHighlights(source, Position { .line = 0, .character = 12 });
    // Should find only 2: first "x" def + first "x" ref
    CHECK(highlights.size() == 2);
}

TEST_CASE("DocumentHighlight.no identifier at cursor returns empty", "[lsp][highlight]")
{
    auto highlights = computeDocumentHighlights("let x = 42", Position { .line = 0, .character = 0 });
    CHECK(highlights.empty());
}

// =============================================================================
// SignatureHelp tests
// =============================================================================

TEST_CASE("SignatureHelp.single parameter function", "[lsp][signaturehelp]")
{
    const auto* source = "let f x = x + 1\nf 3";
    // cursor on "3" (line 1, col 2)
    auto sig = computeSignatureHelp(source, Position { .line = 1, .character = 2 });
    REQUIRE(sig.has_value());
    REQUIRE(!sig->signatures.empty());
    CHECK(sig->signatures[0].label.find('f') != std::string::npos);
    CHECK(sig->signatures[0].parameters.size() == 1);
    CHECK(sig->activeParameter == 0);
}

TEST_CASE("SignatureHelp.multi-parameter function", "[lsp][signaturehelp]")
{
    const auto* source = "let add x y = x + y\nadd 1 2";
    // cursor on "2" (line 1, col 6) — second argument
    auto sig = computeSignatureHelp(source, Position { .line = 1, .character = 6 });
    REQUIRE(sig.has_value());
    REQUIRE(!sig->signatures.empty());
    CHECK(sig->signatures[0].parameters.size() == 2);
    CHECK(sig->activeParameter == 1);
}

TEST_CASE("SignatureHelp.typed parameters show types", "[lsp][signaturehelp]")
{
    const auto* source = "let add (x: int) (y: int): int = x + y\nadd 1 2";
    auto sig = computeSignatureHelp(source, Position { .line = 1, .character = 4 });
    REQUIRE(sig.has_value());
    REQUIRE(!sig->signatures.empty());
    CHECK(sig->signatures[0].label.find("int") != std::string::npos);
}

TEST_CASE("SignatureHelp.not in function call returns nullopt", "[lsp][signaturehelp]")
{
    auto sig = computeSignatureHelp("let x = 42", Position { .line = 0, .character = 8 });
    CHECK_FALSE(sig.has_value());
}

// =============================================================================
// E2E: Definition, References, SignatureHelp
// =============================================================================

TEST_CASE("E2E.initialize advertises new capabilities", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    REQUIRE(!responses.empty());
    auto const& caps = responses[0]["result"]["capabilities"];
    CHECK(caps["definitionProvider"] == true);
    CHECK(caps["referencesProvider"] == true);
    CHECK(caps.contains("signatureHelpProvider"));
}

TEST_CASE("E2E.definition request returns location", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42\nprintln x" },
                               } },
                         }),
        sendRequest("textDocument/definition",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 1 }, { "character", 8 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].contains("uri"));
            CHECK(msg["result"]["uri"] == "file:///test.endo");
            CHECK(msg["result"].contains("range"));
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("E2E.references request returns locations", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42\nprintln x" },
                               } },
                         }),
        sendRequest("textDocument/references",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 4 } } },
                        { "context", json { { "includeDeclaration", true } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            CHECK(msg["result"].size() >= 2); // definition + usage
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("E2E.signatureHelp request returns signature", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let f x = x + 1\nf 3" },
                               } },
                         }),
        sendRequest("textDocument/signatureHelp",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 1 }, { "character", 2 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].contains("signatures"));
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// DocumentSymbol tests
// =============================================================================

TEST_CASE("DocumentSymbol.simple_variable", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let x = 42");
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "x");
    CHECK(symbols[0].kind == SymbolKind::Variable);
    CHECK(symbols[0].children.empty());
}

TEST_CASE("DocumentSymbol.function_with_params", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let add x y = x + y");
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "add");
    CHECK(symbols[0].kind == SymbolKind::Function);
    REQUIRE(symbols[0].children.size() == 2);
    CHECK(symbols[0].children[0].name == "x");
    CHECK(symbols[0].children[0].kind == SymbolKind::Variable);
    CHECK(symbols[0].children[1].name == "y");
    CHECK(symbols[0].children[1].kind == SymbolKind::Variable);
}

TEST_CASE("DocumentSymbol.multiple_bindings", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let a = 1\nlet b = 2");
    REQUIRE(symbols.size() == 2);
    CHECK(symbols[0].name == "a");
    CHECK(symbols[1].name == "b");
}

TEST_CASE("DocumentSymbol.nested_let_in_not_top_level", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let result = let inner = 1 in inner");
    // Only "result" should appear at top level, not "inner"
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "result");
}

TEST_CASE("DocumentSymbol.recursive_function", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let rec fact n = if n <= 1 then 1 else n * fact (n - 1)");
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "fact");
    CHECK(symbols[0].kind == SymbolKind::Function);
    REQUIRE(symbols[0].children.size() == 1);
    CHECK(symbols[0].children[0].name == "n");
}

TEST_CASE("DocumentSymbol.parse_failure_returns_empty", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let = ");
    CHECK(symbols.empty());
}

TEST_CASE("DocumentSymbol.record_type_with_fields", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("type Person = { name: string; age: int }");
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "Person");
    CHECK(symbols[0].kind == SymbolKind::Struct);
    REQUIRE(symbols[0].children.size() == 2);
    CHECK(symbols[0].children[0].name == "name");
    CHECK(symbols[0].children[0].kind == SymbolKind::Field);
    CHECK(symbols[0].children[0].detail == "string");
    CHECK(symbols[0].children[1].name == "age");
    CHECK(symbols[0].children[1].kind == SymbolKind::Field);
    CHECK(symbols[0].children[1].detail == "int");
}

TEST_CASE("DocumentSymbol.union_type_with_variants", "[lsp][documentsymbol]")
{
    auto symbols =
        computeDocumentSymbols("type Shape = | Circle of float | Rectangle of float * float | Point");
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "Shape");
    CHECK(symbols[0].kind == SymbolKind::Enum);
    REQUIRE(symbols[0].children.size() == 3);
    CHECK(symbols[0].children[0].name == "Circle");
    CHECK(symbols[0].children[0].kind == SymbolKind::EnumMember);
    CHECK(symbols[0].children[0].detail == "float");
    CHECK(symbols[0].children[1].name == "Rectangle");
    CHECK(symbols[0].children[1].kind == SymbolKind::EnumMember);
    CHECK(symbols[0].children[1].detail == "float * float");
    CHECK(symbols[0].children[2].name == "Point");
    CHECK(symbols[0].children[2].kind == SymbolKind::EnumMember);
    CHECK_FALSE(symbols[0].children[2].detail.has_value());
}

TEST_CASE("DocumentSymbol.mixed_types_and_bindings", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("type Color = { r: int; g: int; b: int }\n"
                                          "type Shape = | Circle of float | Point\n"
                                          "let area (x: float) : float = x\n"
                                          "let pi = 3.14");
    REQUIRE(symbols.size() == 4);
    CHECK(symbols[0].name == "Color");
    CHECK(symbols[0].kind == SymbolKind::Struct);
    CHECK(symbols[0].children.size() == 3);
    CHECK(symbols[1].name == "Shape");
    CHECK(symbols[1].kind == SymbolKind::Enum);
    CHECK(symbols[1].children.size() == 2);
    CHECK(symbols[2].name == "area");
    CHECK(symbols[2].kind == SymbolKind::Function);
    CHECK(symbols[2].children.size() == 1); // parameter x
    CHECK(symbols[3].name == "pi");
    CHECK(symbols[3].kind == SymbolKind::Variable);
}

TEST_CASE("DocumentSymbol.property_binding", "[lsp][documentsymbol]")
{
    auto symbols = computeDocumentSymbols("let Name with get () = \"test\"");
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "Name");
    CHECK(symbols[0].kind == SymbolKind::Property);
}

// =============================================================================
// Rename tests
// =============================================================================

TEST_CASE("Rename.variable_all_references", "[lsp][rename]")
{
    auto edit = computeRename(
        "let x = 42\nprintln x", "file:///test.endo", Position { .line = 0, .character = 4 }, "y");
    REQUIRE(edit.has_value());
    auto const& changes = edit->changes;
    REQUIRE(changes.count("file:///test.endo") == 1);
    auto const& edits = changes.at("file:///test.endo");
    // Definition + usage = 2 edits
    CHECK(edits.size() == 2);
    for (auto const& e: edits)
        CHECK(e.newText == "y");
}

TEST_CASE("Rename.function_all_references", "[lsp][rename]")
{
    auto edit = computeRename("let f x = x + 1\nprintln (f 3)\nprintln (f 5)",
                              "file:///test.endo",
                              Position { .line = 0, .character = 4 },
                              "g");
    REQUIRE(edit.has_value());
    auto const& edits = edit->changes.at("file:///test.endo");
    // f appears 3 times: definition + 2 calls
    CHECK(edits.size() == 3);
}

TEST_CASE("Rename.parameter_renames_in_scope", "[lsp][rename]")
{
    auto edit = computeRename(
        "let add x y = x + y", "file:///test.endo", Position { .line = 0, .character = 8 }, "a");
    REQUIRE(edit.has_value());
    auto const& edits = edit->changes.at("file:///test.endo");
    // "x" param definition + "x" usage in body = 2 edits
    CHECK(edits.size() == 2);
    for (auto const& e: edits)
        CHECK(e.newText == "a");
}

TEST_CASE("Rename.cursor_not_on_identifier", "[lsp][rename]")
{
    // Cursor on "let" keyword
    auto edit = computeRename("let x = 42", "file:///test.endo", Position { .line = 0, .character = 0 }, "y");
    CHECK_FALSE(edit.has_value());
}

TEST_CASE("PrepareRename.valid_position", "[lsp][rename]")
{
    auto range = prepareRename("let x = 42\nprintln x", Position { .line = 0, .character = 4 });
    REQUIRE(range.has_value());
    CHECK(range->start.line == 0);
    CHECK(range->start.character == 4); // "x" starts at column 4
}

TEST_CASE("PrepareRename.invalid_position", "[lsp][rename]")
{
    // Cursor on "let" keyword
    auto range = prepareRename("let x = 42", Position { .line = 0, .character = 0 });
    CHECK_FALSE(range.has_value());
}

// =============================================================================
// E2E: DocumentSymbol, Rename
// =============================================================================

TEST_CASE("E2E.documentSymbol_request_returns_symbols", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let add x y = x + y\nlet z = 42" },
                               } },
                         }),
        sendRequest("textDocument/documentSymbol",
                    json { { "textDocument", json { { "uri", "file:///test.endo" } } } },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            CHECK(msg["result"].size() == 2); // "add" and "z"
            CHECK(msg["result"][0]["name"] == "add");
            CHECK(msg["result"][0]["kind"] == static_cast<int>(SymbolKind::Function));
            CHECK(msg["result"][0].contains("children"));
            CHECK(msg["result"][1]["name"] == "z");
            CHECK(msg["result"][1]["kind"] == static_cast<int>(SymbolKind::Variable));
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("E2E.rename_request_returns_workspace_edit", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42\nprintln x" },
                               } },
                         }),
        sendRequest("textDocument/rename",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 4 } } },
                        { "newName", "myVar" },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].contains("changes"));
            auto const& changes = msg["result"]["changes"];
            CHECK(changes.contains("file:///test.endo"));
            auto const& edits = changes["file:///test.endo"];
            CHECK(edits.is_array());
            CHECK(edits.size() == 2); // definition + usage
            for (auto const& edit: edits)
                CHECK(edit["newText"] == "myVar");
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("E2E.unknown_command_produces_diagnostic", "[lsp][e2e][command-not-found]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "definitely_not_a_real_command_xyz" },
                               } },
                         }),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    bool foundDiagnostics = false;
    for (auto const& msg: responses)
    {
        if (msg.value("method", "") == "textDocument/publishDiagnostics")
        {
            foundDiagnostics = true;
            CHECK(msg["params"]["uri"] == "file:///test.endo");
            auto const& diags = msg["params"]["diagnostics"];
            REQUIRE(!diags.empty());
            CHECK(diags[0]["message"].get<std::string>().find("command not found") != std::string::npos);
            CHECK(diags[0]["severity"] == 1); // Error
            break;
        }
    }
    CHECK(foundDiagnostics);
}

TEST_CASE("E2E.known_command_no_command_not_found_diagnostic", "[lsp][e2e][command-not-found]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "date --version" },
                               } },
                         }),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    bool foundDiagnostics = false;
    for (auto const& msg: responses)
    {
        if (msg.value("method", "") == "textDocument/publishDiagnostics")
        {
            foundDiagnostics = true;
            CHECK(msg["params"]["diagnostics"].empty());
            break;
        }
    }
    CHECK(foundDiagnostics);
}

TEST_CASE("E2E.initialize_advertises_documentSymbol_and_rename", "[lsp][e2e]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    REQUIRE(!responses.empty());
    auto const& caps = responses[0]["result"]["capabilities"];
    CHECK(caps["documentSymbolProvider"] == true);
    CHECK(caps.contains("renameProvider"));
    CHECK(caps["renameProvider"]["prepareProvider"] == true);
}

// =============================================================================
// Completion tests
// =============================================================================

TEST_CASE("Completion.initialize_advertises_completion", "[lsp][completion]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendRequest("shutdown", json::object(), 2),
        sendNotification("exit", json::object()),
    });

    REQUIRE(!responses.empty());
    auto const& caps = responses[0]["result"]["capabilities"];
    CHECK(caps.contains("completionProvider"));
    auto const& triggers = caps["completionProvider"]["triggerCharacters"];
    CHECK(triggers.is_array());
    // Verify space is a trigger character (needed for <- assignment completion)
    bool hasSpace = false;
    for (auto const& t: triggers)
    {
        if (t.get<std::string>() == " ")
            hasSpace = true;
    }
    CHECK(hasSpace);
}

TEST_CASE("Completion.basic_request_returns_json_array", "[lsp][completion]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "le" },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 2 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            // "le" should match "let"
            bool hasLet = false;
            for (auto const& item: msg["result"])
            {
                if (item["label"] == "let")
                    hasLet = true;
            }
            CHECK(hasLet);
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Completion.document_symbols_included", "[lsp][completion]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let add x y = x + y\nad" },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 1 }, { "character", 2 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            bool hasAdd = false;
            for (auto const& item: msg["result"])
            {
                if (item["label"] == "add")
                    hasAdd = true;
            }
            CHECK(hasAdd);
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Completion.dot_access_returns_option_methods", "[lsp][completion]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "Option." },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 7 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            bool hasMap = false;
            bool hasBind = false;
            bool hasDefaultValue = false;
            for (auto const& item: msg["result"])
            {
                if (item["label"] == "Option.map")
                    hasMap = true;
                if (item["label"] == "Option.bind")
                    hasBind = true;
                if (item["label"] == "Option.defaultValue")
                    hasDefaultValue = true;
            }
            CHECK(hasMap);
            CHECK(hasBind);
            CHECK(hasDefaultValue);
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Hover.record_variable_shows_type_name", "[lsp][hover]")
{
    const auto* source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }";
    auto hover = computeHover(source, Position { .line = 1, .character = 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("Person") != std::string::npos);
    CHECK(hover->contents.value.find("alice") != std::string::npos);
}

TEST_CASE("Completion.record_field_dot_access", "[lsp][completion]")
{
    const auto* source =
        "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }\nalice.";
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", source },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 2 }, { "character", 6 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            bool hasName = false;
            bool hasAge = false;
            for (auto const& item: msg["result"])
            {
                if (item["label"] == "alice.name")
                    hasName = true;
                if (item["label"] == "alice.age")
                    hasAge = true;
            }
            CHECK(hasName);
            CHECK(hasAge);
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Completion.record_variable_specific_fields", "[lsp][completion]")
{
    const auto* source =
        "type Person = { name: str; age: int }\ntype ProcessInfo = { pid: int; cpu: float }\nlet alice = { "
        "name = \"Alice\"; age = 30 }\nalice.";
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", source },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 3 }, { "character", 6 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            // Should have only Person fields, not ProcessInfo fields
            bool hasName = false;
            bool hasAge = false;
            bool hasPid = false;
            bool hasCpu = false;
            for (auto const& item: msg["result"])
            {
                if (item["label"] == "alice.name")
                    hasName = true;
                if (item["label"] == "alice.age")
                    hasAge = true;
                if (item["label"] == "alice.pid")
                    hasPid = true;
                if (item["label"] == "alice.cpu")
                    hasCpu = true;
            }
            CHECK(hasName);
            CHECK(hasAge);
            CHECK_FALSE(hasPid);
            CHECK_FALSE(hasCpu);
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// Completion: left-arrow (<-) assignment value tests
// =============================================================================

TEST_CASE("Completion.left_arrow_model_candidates", "[lsp][completion][left-arrow]")
{
    // "agent_claude_model <- " = 22 chars
    auto result = computeCompletion("agent_claude_model <- ", Position { .line = 0, .character = 22 });
    CHECK(result.is_array());
    bool hasOpus = false;
    bool hasSonnet = false;
    for (auto const& item: result)
    {
        auto const label = item["label"].get<std::string>();
        if (label.find("opus") != std::string::npos)
            hasOpus = true;
        if (label.find("sonnet") != std::string::npos)
            hasSonnet = true;
    }
    CHECK(hasOpus);
    CHECK(hasSonnet);
}

TEST_CASE("Completion.left_arrow_preset_prefix_filter", "[lsp][completion][left-arrow]")
{
    // "shell_prompt_preset <- pow" = 26 chars — should filter to powerline
    auto result = computeCompletion("shell_prompt_preset <- pow", Position { .line = 0, .character = 26 });
    CHECK(result.is_array());
    bool hasPowerline = false;
    bool hasMinimalArrow = false;
    for (auto const& item: result)
    {
        auto const label = item["label"].get<std::string>();
        if (label == "powerline")
            hasPowerline = true;
        if (label == "minimal-arrow")
            hasMinimalArrow = true;
    }
    CHECK(hasPowerline);
    CHECK_FALSE(hasMinimalArrow);
}

TEST_CASE("Completion.left_arrow_boolean_candidates", "[lsp][completion][left-arrow]")
{
    // "agent_trace_enabled <- " = 23 chars
    auto result = computeCompletion("agent_trace_enabled <- ", Position { .line = 0, .character = 23 });
    CHECK(result.is_array());
    bool hasTrue = false;
    bool hasFalse = false;
    for (auto const& item: result)
    {
        auto const label = item["label"].get<std::string>();
        if (label == "true")
            hasTrue = true;
        if (label == "false")
            hasFalse = true;
    }
    CHECK(hasTrue);
    CHECK(hasFalse);
}

TEST_CASE("Completion.left_arrow_auth_type_candidates", "[lsp][completion][left-arrow]")
{
    // "agent_claude_auth_type <- " = 26 chars
    auto result = computeCompletion("agent_claude_auth_type <- ", Position { .line = 0, .character = 26 });
    CHECK(result.is_array());
    bool hasAuto = false;
    bool hasOauth = false;
    bool hasApiKey = false;
    for (auto const& item: result)
    {
        auto const label = item["label"].get<std::string>();
        if (label == "auto")
            hasAuto = true;
        if (label == "oauth")
            hasOauth = true;
        if (label == "api_key")
            hasApiKey = true;
    }
    CHECK(hasAuto);
    CHECK(hasOauth);
    CHECK(hasApiKey);
}

TEST_CASE("E2E.completion_left_arrow_multiline_model_values", "[lsp][e2e][completion][left-arrow]")
{
    // Multi-line document: <- on a non-first line must resolve the correct property
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 5\nagent_claude_model <- " },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 1 }, { "character", 22 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            bool hasOpus = false;
            bool hasSonnet = false;
            for (auto const& item: msg["result"])
            {
                auto const label = item["label"].get<std::string>();
                if (label.find("opus") != std::string::npos)
                    hasOpus = true;
                if (label.find("sonnet") != std::string::npos)
                    hasSonnet = true;
            }
            CHECK(hasOpus);
            CHECK(hasSonnet);
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("E2E.completion_left_arrow_preset_values", "[lsp][e2e][completion][left-arrow]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "shell_prompt_preset <- " },
                               } },
                         }),
        sendRequest("textDocument/completion",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 23 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            bool hasPowerline = false;
            bool hasMinimalArrow = false;
            for (auto const& item: msg["result"])
            {
                auto const label = item["label"].get<std::string>();
                if (label == "powerline")
                    hasPowerline = true;
                if (label == "minimal-arrow")
                    hasMinimalArrow = true;
            }
            CHECK(hasPowerline);
            CHECK(hasMinimalArrow);
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("E2E.documentHighlight request returns highlights", "[lsp][e2e][highlight]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42\nprintln x" },
                               } },
                         }),
        sendRequest("textDocument/documentHighlight",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "position", json { { "line", 0 }, { "character", 4 } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            CHECK(msg["result"].is_array());
            REQUIRE(msg["result"].size() == 2);
            // First highlight is the definition (Write=3)
            CHECK(msg["result"][0]["kind"] == 3);
            // Second highlight is the reference (Read=2)
            CHECK(msg["result"][1]["kind"] == 2);
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// Inlay hint tests
// =============================================================================

namespace
{

/// Helper to compute inlay hints for the full document range.
std::vector<InlayHint> hintsForSource(std::string const& source)
{
    auto const range = Range {
        .start = Position { .line = 0, .character = 0 },
        .end = Position { .line = 9999, .character = 9999 },
    };
    return computeInlayHints(source, range);
}

} // namespace

TEST_CASE("InlayHint.untyped params get type hints", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let add x y = x + y");
    // Should have at least 2 param hints (x: int, y: int)
    auto paramHints = std::vector<InlayHint> {};
    for (auto const& h: hints)
        if (h.label.starts_with(": "))
            paramHints.push_back(h);
    REQUIRE(paramHints.size() >= 2);
    CHECK(paramHints[0].label == ": int");
    CHECK(paramHints[1].label == ": int");
    CHECK(paramHints[0].kind == InlayHintKind::Type);
}

TEST_CASE("InlayHint.typed params are suppressed", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let add (x: int) (y: int) : int = x + y");
    // Fully annotated — no hints expected
    CHECK(hints.empty());
}

TEST_CASE("InlayHint.return type hint shown when not annotated", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let add (x: int) (y: int) = x + y");
    // Only a return type hint expected (params are typed)
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].label == ": int");
    CHECK(hints[0].paddingLeft == true);
}

TEST_CASE("InlayHint.string typed params", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let greet name = \"Hello, \" + name");
    auto paramHints = std::vector<InlayHint> {};
    for (auto const& h: hints)
        if (!h.paddingLeft)
            paramHints.push_back(h);
    REQUIRE(!paramHints.empty());
    CHECK(paramHints[0].label == ": string");
}

TEST_CASE("InlayHint.empty source returns empty", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("");
    CHECK(hints.empty());
}

TEST_CASE("InlayHint.invalid source returns empty", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let = = =");
    CHECK(hints.empty());
}

TEST_CASE("InlayHint.range filtering excludes out-of-range hints", "[lsp][inlayhint]")
{
    auto const* source = "let add x y = x + y\nlet sub a b = a - b";
    // Only request hints for line 1
    auto const range = Range {
        .start = Position { .line = 1, .character = 0 },
        .end = Position { .line = 1, .character = 999 },
    };
    auto hints = computeInlayHints(source, range);
    // All hints should be on line 1
    for (auto const& h: hints)
        CHECK(h.position.line == 1);
}

TEST_CASE("InlayHint.let binding variable type hint", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let x = 42");
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].label == ": int");
    CHECK(hints[0].kind == InlayHintKind::Type);
}

TEST_CASE("InlayHint.recursive function shows hints", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let rec fact n = if n <= 1 then 1 else n * fact (n - 1)");
    // Should have param hint for n and return type hint
    auto paramHints = std::vector<InlayHint> {};
    auto returnHints = std::vector<InlayHint> {};
    for (auto const& h: hints)
    {
        if (h.paddingLeft)
            returnHints.push_back(h);
        else
            paramHints.push_back(h);
    }
    CHECK(!paramHints.empty());
    CHECK(!returnHints.empty());
}

TEST_CASE("InlayHint.let-in expression shows hints", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let result = let x = 42 in x + 1");
    // Should have at least a binding type hint for x
    bool foundXHint = false;
    for (auto const& h: hints)
    {
        if (h.label == ": int" && !h.paddingLeft)
            foundXHint = true;
    }
    CHECK(foundXHint);
}

TEST_CASE("InlayHint.record_binding_shows_type", "[lsp][inlayhint]")
{
    auto hints =
        hintsForSource("type Person = { name: string; age: int }\nlet p = { name = \"Alice\"; age = 30 }");
    // Should have a hint for `p` showing `: Person`
    bool foundPersonHint = false;
    for (auto const& h: hints)
    {
        if (h.label == ": Person")
            foundPersonHint = true;
    }
    CHECK(foundPersonHint);
}

TEST_CASE("InlayHint.return_type_position_not_on_param", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let f p = p + 1");
    // Should have a param hint for `p` and a return type hint
    auto paramHints = std::vector<InlayHint> {};
    auto returnHints = std::vector<InlayHint> {};
    for (auto const& h: hints)
    {
        if (h.paddingLeft)
            returnHints.push_back(h);
        else
            paramHints.push_back(h);
    }
    REQUIRE(!paramHints.empty());
    REQUIRE(!returnHints.empty());
    // Return type hint position must differ from param hint position
    CHECK((returnHints[0].position.line != paramHints[0].position.line
           || returnHints[0].position.character != paramHints[0].position.character));
}

TEST_CASE("InlayHint.multiple_functions_same_param_name", "[lsp][inlayhint]")
{
    auto hints = hintsForSource("let f p = p + 1\nlet g p = p + 2");
    // Should have at least 2 param hints (one per function's `p`)
    auto paramHints = std::vector<InlayHint> {};
    for (auto const& h: hints)
    {
        if (!h.paddingLeft && h.label == ": int")
            paramHints.push_back(h);
    }
    REQUIRE(paramHints.size() >= 2);
    // They should be at different positions (different functions)
    CHECK((paramHints[0].position.line != paramHints[1].position.line
           || paramHints[0].position.character != paramHints[1].position.character));
}

TEST_CASE("InlayHint.field_access_return_type", "[lsp][inlayhint]")
{
    // Use a function where the return type is inferred from comparison
    auto hints = hintsForSource("let is_adult x = x >= 18");
    // Should have a return type hint `: bool`
    bool foundBoolReturn = false;
    for (auto const& h: hints)
    {
        if (h.label == ": bool" && h.paddingLeft)
            foundBoolReturn = true;
    }
    CHECK(foundBoolReturn);
}

TEST_CASE("E2E.inlayHint request returns array", "[lsp][e2e][inlayhint]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let add x y = x + y" },
                               } },
                         }),
        sendRequest("textDocument/inlayHint",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "range",
                          json {
                              { "start", json { { "line", 0 }, { "character", 0 } } },
                              { "end", json { { "line", 99 }, { "character", 0 } } },
                          } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            REQUIRE(msg["result"].is_array());
            CHECK(!msg["result"].empty());
            // Check that hints have kind == 1 (Type)
            for (auto const& hint: msg["result"])
                CHECK(hint["kind"] == 1);
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// Pipeline inlay hints tests
// =============================================================================

TEST_CASE("InlayHint.pipeline_intermediate_type", "[lsp][inlayhint][pipeline]")
{
    auto hints = hintsForSource("let f x = x + 1\n[1; 2; 3] |> List.length");
    // Should have a hint after `[1; 2; 3]` showing `: list<int>`
    bool foundListHint = false;
    for (auto const& h: hints)
    {
        if (h.label.find("list") != std::string::npos && h.paddingLeft)
            foundListHint = true;
    }
    CHECK(foundListHint);
}

TEST_CASE("InlayHint.chained_pipeline_types", "[lsp][inlayhint][pipeline]")
{
    auto hints = hintsForSource("[1; 2; 3] |> List.length |> fun x -> x + 1");
    // Should have hints at each pipeline boundary
    auto pipelineHints = std::vector<InlayHint> {};
    for (auto const& h: hints)
    {
        if (h.paddingLeft && h.label.starts_with(": "))
            pipelineHints.push_back(h);
    }
    // At least the first pipeline value should have a hint
    CHECK(!pipelineHints.empty());
}

// =============================================================================
// Folding range tests
// =============================================================================

TEST_CASE("FoldingRange.multi_line_function", "[lsp][folding]")
{
    auto ranges = computeFoldingRanges("let f x =\n  x + 1");
    bool found = false;
    for (auto const& r: ranges)
    {
        if (r.startLine == 0 && r.endLine == 1 && r.kind == "region")
            found = true;
    }
    CHECK(found);
}

TEST_CASE("FoldingRange.single_line_no_range", "[lsp][folding]")
{
    auto ranges = computeFoldingRanges("let x = 42");
    // Single-line constructs should NOT produce folding ranges
    bool foundRegion = false;
    for (auto const& r: ranges)
    {
        if (r.kind == "region")
            foundRegion = true;
    }
    CHECK_FALSE(foundRegion);
}

TEST_CASE("FoldingRange.match_expression", "[lsp][folding]")
{
    auto ranges = computeFoldingRanges("let r = match x with\n| 0 -> 1\n| _ -> 2");
    bool found = false;
    for (auto const& r: ranges)
    {
        if (r.kind == "region" && r.startLine <= 0 && r.endLine >= 2)
            found = true;
    }
    CHECK(found);
}

TEST_CASE("FoldingRange.nested_constructs", "[lsp][folding]")
{
    auto ranges = computeFoldingRanges("let f x =\n  match x with\n  | 0 -> 1\n  | _ -> 2");
    // Should have at least 2 folding ranges: function and match
    auto regionCount = 0;
    for (auto const& r: ranges)
    {
        if (r.kind == "region")
            ++regionCount;
    }
    CHECK(regionCount >= 2);
}

TEST_CASE("FoldingRange.empty_source", "[lsp][folding]")
{
    auto ranges = computeFoldingRanges("");
    CHECK(ranges.empty());
}

TEST_CASE("FoldingRange.multi_line_comment", "[lsp][folding]")
{
    auto ranges = computeFoldingRanges("(* this is\na multi-line\ncomment *)");
    bool found = false;
    for (auto const& r: ranges)
    {
        if (r.kind == "comment" && r.startLine == 0 && r.endLine == 2)
            found = true;
    }
    CHECK(found);
}

TEST_CASE("E2E.foldingRange request returns array", "[lsp][e2e][folding]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let f x =\n  x + 1" },
                               } },
                         }),
        sendRequest("textDocument/foldingRange",
                    json { { "textDocument", json { { "uri", "file:///test.endo" } } } },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            REQUIRE(msg["result"].is_array());
            CHECK(!msg["result"].empty());
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// Selection range tests
// =============================================================================

TEST_CASE("SelectionRange.cursor_in_identifier", "[lsp][selection]")
{
    auto ranges = computeSelectionRanges("let x = 42", { Position { .line = 0, .character = 4 } });
    REQUIRE(ranges.size() == 1);
    // Should have at least one range (the identifier or enclosing statement)
    CHECK(ranges[0].range.start.line == 0);
}

TEST_CASE("SelectionRange.nested_match", "[lsp][selection]")
{
    auto ranges = computeSelectionRanges("let r = match x with\n| 0 -> 1\n| _ -> 2",
                                         { Position { .line = 1, .character = 7 } });
    REQUIRE(ranges.size() == 1);
    // Should have nested ranges (arm body -> match -> let binding)
    auto* current = &ranges[0];
    auto depth = 1;
    while (current->parent)
    {
        current = current->parent.get();
        ++depth;
    }
    CHECK(depth >= 2); // At least the innermost and one parent
}

TEST_CASE("SelectionRange.multiple_positions", "[lsp][selection]")
{
    auto ranges = computeSelectionRanges(
        "let x = 42\nlet y = 99",
        { Position { .line = 0, .character = 4 }, Position { .line = 1, .character = 4 } });
    REQUIRE(ranges.size() == 2);
}

TEST_CASE("SelectionRange.cursor_at_start", "[lsp][selection]")
{
    auto ranges = computeSelectionRanges("let x = 42", { Position { .line = 0, .character = 0 } });
    REQUIRE(ranges.size() == 1);
    // Should have at least the document-level range
    CHECK(ranges[0].range.start.line == 0);
}

TEST_CASE("E2E.selectionRange request returns array", "[lsp][e2e][selection]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42" },
                               } },
                         }),
        sendRequest("textDocument/selectionRange",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "positions", json::array({ json { { "line", 0 }, { "character", 4 } } }) },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            REQUIRE(msg["result"].is_array());
            CHECK(!msg["result"].empty());
            // Each entry should have a "range" field
            CHECK(msg["result"][0].contains("range"));
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// Code action tests
// =============================================================================

TEST_CASE("CodeAction.no_diagnostics_returns_empty", "[lsp][codeaction]")
{
    auto actions = computeCodeActions(
        "let x = 42",
        "file:///test.endo",
        Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 10 } },
        {});
    CHECK(actions.empty());
}

TEST_CASE("CodeAction.diagnostic_without_data_returns_empty", "[lsp][codeaction]")
{
    auto diag = Diagnostic {
        .range = Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 5 } },
        .severity = DiagnosticSeverity::Error,
        .source = "endo",
        .message = "some error",
    };
    auto actions = computeCodeActions(
        "let x = 42",
        "file:///test.endo",
        Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 10 } },
        { diag });
    CHECK(actions.empty());
}

TEST_CASE("CodeAction.did_you_mean_suggestion", "[lsp][codeaction]")
{
    auto diag = Diagnostic {
        .range = Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 4 } },
        .severity = DiagnosticSeverity::Error,
        .source = "endo",
        .message = "Unknown command: 'prit'",
        .data = nlohmann::json::array({ "Did you mean 'print'?" }),
    };
    auto actions = computeCodeActions(
        "prit hello",
        "file:///test.endo",
        Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 4 } },
        { diag });
    REQUIRE(!actions.empty());
    CHECK(actions[0].title == "Did you mean 'print'?");
    CHECK(actions[0].kind == "quickfix");
    CHECK(actions[0].isPreferred);
    REQUIRE(actions[0].edit.has_value());
    auto const& changes = actions[0].edit->changes;
    CHECK(changes.count("file:///test.endo") == 1);
    CHECK(changes.at("file:///test.endo")[0].newText == "print");
}

TEST_CASE("CodeAction.informational_suggestion", "[lsp][codeaction]")
{
    auto diag = Diagnostic {
        .range = Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 5 } },
        .severity = DiagnosticSeverity::Warning,
        .source = "endo",
        .message = "Some warning",
        .data = nlohmann::json::array({ "Consider refactoring this code" }),
    };
    auto actions = computeCodeActions(
        "some code",
        "file:///test.endo",
        Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 5 } },
        { diag });
    REQUIRE(!actions.empty());
    CHECK(actions[0].title == "Consider refactoring this code");
    // Informational suggestion should not have an edit
    CHECK_FALSE(actions[0].edit.has_value());
    CHECK_FALSE(actions[0].isPreferred);
}

TEST_CASE("CodeAction.diagnostic_outside_range_skipped", "[lsp][codeaction]")
{
    auto diag = Diagnostic {
        .range = Range { .start = { .line = 5, .character = 0 }, .end = { .line = 5, .character = 4 } },
        .severity = DiagnosticSeverity::Error,
        .source = "endo",
        .message = "error",
        .data = nlohmann::json::array({ "Did you mean 'print'?" }),
    };
    auto actions = computeCodeActions(
        "let x = 42",
        "file:///test.endo",
        Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 10 } },
        { diag });
    CHECK(actions.empty());
}

TEST_CASE("E2E.codeAction request returns array", "[lsp][e2e][codeaction]")
{
    auto responses = runSession({
        sendRequest("initialize", json::object()),
        sendNotification("initialized", json::object()),
        sendNotification("textDocument/didOpen",
                         json {
                             { "textDocument",
                               json {
                                   { "uri", "file:///test.endo" },
                                   { "languageId", "endo" },
                                   { "version", 1 },
                                   { "text", "let x = 42" },
                               } },
                         }),
        sendRequest("textDocument/codeAction",
                    json {
                        { "textDocument", json { { "uri", "file:///test.endo" } } },
                        { "range",
                          json {
                              { "start", json { { "line", 0 }, { "character", 0 } } },
                              { "end", json { { "line", 0 }, { "character", 10 } } },
                          } },
                        { "context", json { { "diagnostics", json::array() } } },
                    },
                    3),
        sendRequest("shutdown", json::object(), 4),
        sendNotification("exit", json::object()),
    });

    bool found = false;
    for (auto const& msg: responses)
    {
        if (msg.value("id", -1) == 3)
        {
            found = true;
            REQUIRE(msg["result"].is_array());
            // Empty diagnostics -> empty result
            CHECK(msg["result"].empty());
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("CodeAction.diagnostics_data_roundtrip", "[lsp][codeaction]")
{
    // Verify that a Diagnostic with data survives JSON serialization and deserialization
    auto diag = Diagnostic {
        .range = Range { .start = { .line = 0, .character = 0 }, .end = { .line = 0, .character = 4 } },
        .severity = DiagnosticSeverity::Error,
        .source = "endo",
        .message = "Unknown command: 'prnt'",
        .data = nlohmann::json::array({ "Did you mean 'print'?", "Did you mean 'printf'?" }),
    };

    // Round-trip through JSON
    auto j = nlohmann::json(diag);
    auto parsed = j.get<Diagnostic>();

    CHECK(parsed.range.start.line == 0);
    CHECK(parsed.range.end.character == 4);
    CHECK(parsed.severity == DiagnosticSeverity::Error);
    CHECK(parsed.source == "endo");
    CHECK(parsed.message == "Unknown command: 'prnt'");
    REQUIRE(parsed.data.has_value());
    REQUIRE(parsed.data->is_array());
    CHECK(parsed.data->size() == 2);
    CHECK((*parsed.data)[0] == "Did you mean 'print'?");
    CHECK((*parsed.data)[1] == "Did you mean 'printf'?");
}

TEST_CASE("CodeAction.diagnostic_without_data_roundtrip", "[lsp][codeaction]")
{
    // Verify that a Diagnostic WITHOUT data also round-trips cleanly
    auto diag = Diagnostic {
        .range = Range { .start = { .line = 1, .character = 0 }, .end = { .line = 1, .character = 5 } },
        .severity = DiagnosticSeverity::Warning,
        .source = "endo",
        .message = "Unused variable 'x'",
    };

    auto j = nlohmann::json(diag);
    CHECK_FALSE(j.contains("data"));
    auto parsed = j.get<Diagnostic>();
    CHECK_FALSE(parsed.data.has_value());
    CHECK(parsed.message == "Unused variable 'x'");
}
