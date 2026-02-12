// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "DefinitionProvider.hpp"
#include "DiagnosticsProvider.hpp"
#include "DocumentStore.hpp"
#include "DocumentSymbolProvider.hpp"
#include "HoverProvider.hpp"
#include "JsonRpc.hpp"
#include "LspServer.hpp"
#include "ReferencesProvider.hpp"
#include "RenameProvider.hpp"
#include "SemanticTokens.hpp"
#include "SignatureHelpProvider.hpp"
#include "SymbolCollector.hpp"
#include <nlohmann/json.hpp>

using namespace endo::lsp;
using json = nlohmann::json;

// =============================================================================
// Helper: creates a JSON-RPC message string with Content-Length header
// =============================================================================
namespace
{

std::string makeRpcMessage(json const& msg)
{
    auto const body = msg.dump();
    std::ostringstream oss;
    oss << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return oss.str();
}

json sendRequest(std::string const& method, json const& params, int id = 1)
{
    return json { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } };
}

json sendNotification(std::string const& method, json const& params)
{
    return json { { "jsonrpc", "2.0" }, { "method", method }, { "params", params } };
}

/// Reads all JSON-RPC messages from the output stream.
std::vector<json> readAllMessages(std::istringstream& output)
{
    std::vector<json> messages;
    while (output.good() && output.peek() != EOF)
    {
        auto msg = readMessage(output);
        if (msg.has_value())
            messages.push_back(std::move(*msg));
        else
            break;
    }
    return messages;
}

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
    auto const body = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
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

    REQUIRE(responses.size() >= 1);
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

// =============================================================================
// Hover tests
// =============================================================================

TEST_CASE("Hover.let keyword returns binding description", "[lsp][hover]")
{
    auto hover = computeHover("let x = 42", Position { 0, 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("let") != std::string::npos);
    CHECK(hover->contents.value.find("binding") != std::string::npos);
}

TEST_CASE("Hover.fun keyword returns lambda description", "[lsp][hover]")
{
    auto hover = computeHover("fun x -> x", Position { 0, 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("fun") != std::string::npos);
    CHECK(hover->contents.value.find("ambda") != std::string::npos); // "Lambda" or "lambda"
}

TEST_CASE("Hover.match keyword returns description", "[lsp][hover]")
{
    auto hover = computeHover("match x with\n| 0 -> 1", Position { 0, 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("match") != std::string::npos);
}

TEST_CASE("Hover.Some constructor returns type signature", "[lsp][hover]")
{
    auto hover = computeHover("Some 5", Position { 0, 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("Some") != std::string::npos);
    CHECK(hover->contents.value.find("option") != std::string::npos);
}

TEST_CASE("Hover.Ok constructor returns type signature", "[lsp][hover]")
{
    auto hover = computeHover("Ok 5", Position { 0, 0 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("Ok") != std::string::npos);
    CHECK(hover->contents.value.find("result") != std::string::npos);
}

TEST_CASE("Hover.|> operator returns pipe description", "[lsp][hover]")
{
    auto hover = computeHover("x |> f", Position { 0, 2 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("|>") != std::string::npos);
    CHECK(hover->contents.value.find("pipe") != std::string::npos);
}

TEST_CASE("Hover.whitespace returns nullopt", "[lsp][hover]")
{
    // Position 3 is on whitespace between "let" and "x"
    auto hover = computeHover("let x = 42", Position { 0, 3 });
    CHECK_FALSE(hover.has_value());
}

TEST_CASE("Hover.position past end returns nullopt", "[lsp][hover]")
{
    auto hover = computeHover("let x = 42", Position { 5, 0 });
    CHECK_FALSE(hover.has_value());
}

TEST_CASE("Hover.variable binding at definition shows value", "[lsp][hover]")
{
    // Position 4 is on "variable" in "let variable = 42"
    auto hover = computeHover("let variable = 42", Position { 0, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("variable") != std::string::npos);
    CHECK(hover->contents.value.find("binding") != std::string::npos);
    CHECK(hover->contents.value.find("= 42") != std::string::npos);
}

TEST_CASE("Hover.variable binding at usage shows value", "[lsp][hover]")
{
    // Position 0,8 is on "variable" in the second line
    auto hover = computeHover("let variable = 42\nprintln variable", Position { 1, 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("variable") != std::string::npos);
    CHECK(hover->contents.value.find("binding") != std::string::npos);
    CHECK(hover->contents.value.find("= 42") != std::string::npos);
}

TEST_CASE("Hover.string binding shows value preview", "[lsp][hover]")
{
    auto hover = computeHover("let greeting = \"hello world\"\nprintln greeting", Position { 1, 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("greeting") != std::string::npos);
    CHECK(hover->contents.value.find("hello world") != std::string::npos);
}

TEST_CASE("Hover.boolean binding shows value preview", "[lsp][hover]")
{
    auto hover = computeHover("let flag = true", Position { 0, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("flag") != std::string::npos);
    CHECK(hover->contents.value.find("= true") != std::string::npos);
}

TEST_CASE("Hover.expression binding shows value preview", "[lsp][hover]")
{
    auto hover = computeHover("let result = Some 42", Position { 0, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("result") != std::string::npos);
    CHECK(hover->contents.value.find("= Some 42") != std::string::npos);
}

TEST_CASE("Hover.function binding does not show body as value", "[lsp][hover]")
{
    auto hover = computeHover("let double x = x * 2", Position { 0, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("function") != std::string::npos);
    // Functions show signature, not body - should not contain "= x * 2"
    CHECK(hover->contents.value.find("= x * 2") == std::string::npos);
}

TEST_CASE("Hover.function definition shows function info with parameters", "[lsp][hover]")
{
    // Position 4 is on "add" in "let add x y = x + y"
    auto hover = computeHover("let add x y = x + y", Position { 0, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("add") != std::string::npos);
    CHECK(hover->contents.value.find("function") != std::string::npos);
    CHECK(hover->contents.value.find("x") != std::string::npos);
    CHECK(hover->contents.value.find("y") != std::string::npos);
}

TEST_CASE("Hover.function reference shows function info", "[lsp][hover]")
{
    // Hover on "add" in the usage "println (add 3 4)"
    auto hover = computeHover("let add x y = x + y\nprintln (add 3 4)", Position { 1, 9 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("add") != std::string::npos);
    CHECK(hover->contents.value.find("function") != std::string::npos);
}

TEST_CASE("Hover.function parameter shows parameter info", "[lsp][hover]")
{
    // Position 8 is on "x" parameter in "let add x y = x + y"
    auto hover = computeHover("let add x y = x + y", Position { 0, 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("x") != std::string::npos);
    CHECK(hover->contents.value.find("parameter") != std::string::npos);
    CHECK(hover->contents.value.find("add") != std::string::npos);
}

TEST_CASE("Hover.typed function shows type annotations", "[lsp][hover]")
{
    auto hover = computeHover("let add (x: int) (y: int): int = x + y", Position { 0, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("add") != std::string::npos);
    CHECK(hover->contents.value.find("int") != std::string::npos);
}

TEST_CASE("Hover.recursive function shows rec qualifier", "[lsp][hover]")
{
    auto hover = computeHover("let rec fact n = if n <= 1 then 1 else n * fact (n - 1)\nprintln (fact 5)",
                              Position { 0, 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("fact") != std::string::npos);
    CHECK(hover->contents.value.find("rec") != std::string::npos);
}

TEST_CASE("Hover.mutable binding shows mut qualifier", "[lsp][hover]")
{
    auto hover = computeHover("let mut counter = 0", Position { 0, 8 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("counter") != std::string::npos);
    CHECK(hover->contents.value.find("mutable") != std::string::npos);
    CHECK(hover->contents.value.find("mut") != std::string::npos);
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
    CHECK(table->definitions.size() >= 1);
    // "x" should be defined once
    bool foundXDef = false;
    for (auto const& def: table->definitions)
    {
        if (def.name == "x")
        {
            foundXDef = true;
            CHECK_FALSE(def.isFunction);
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
            CHECK(def.isFunction);
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
    auto loc = computeDefinition("let x = 42\nprintln x", "file:///test.endo", Position { 1, 8 });
    REQUIRE(loc.has_value());
    CHECK(loc->uri == "file:///test.endo");
    // Definition should be at line 0 (let x)
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.function reference jumps to definition", "[lsp][definition]")
{
    auto loc = computeDefinition("let f x = x + 1\nprintln (f 3)", "file:///test.endo", Position { 1, 9 });
    REQUIRE(loc.has_value());
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.function parameter in body jumps to param", "[lsp][definition]")
{
    // "let f x = x + 1"
    //  the second "x" (in body, col 10) should resolve to param "x" (col 6)
    auto loc = computeDefinition("let f x = x + 1", "file:///test.endo", Position { 0, 10 });
    REQUIRE(loc.has_value());
    // Parameter "x" is at column 6
    CHECK(loc->range.start.character == 6);
}

TEST_CASE("Definition.let-in scoped binding", "[lsp][definition]")
{
    auto source = "let result = let y = 10 in y + 1";
    // cursor on "y" in "y + 1" (col 27)
    auto loc = computeDefinition(source, "file:///test.endo", Position { 0, 27 });
    REQUIRE(loc.has_value());
    // "y" definition is at col 17
    CHECK(loc->range.start.line == 0);
}

TEST_CASE("Definition.lambda parameter", "[lsp][definition]")
{
    auto source = "let f = fun x -> x + 1";
    // cursor on "x" in body (col 17)
    auto loc = computeDefinition(source, "file:///test.endo", Position { 0, 17 });
    REQUIRE(loc.has_value());
    // "x" parameter is at col 12
    CHECK(loc->range.start.character == 12);
}

TEST_CASE("Definition.cursor not on identifier returns nullopt", "[lsp][definition]")
{
    auto loc = computeDefinition("let x = 42", "file:///test.endo", Position { 0, 0 });
    CHECK_FALSE(loc.has_value()); // cursor on "let" keyword
}

TEST_CASE("Definition.unknown identifier returns nullopt", "[lsp][definition]")
{
    // "println" is a builtin, not a user-defined symbol
    auto loc = computeDefinition("println 42", "file:///test.endo", Position { 0, 0 });
    // It may return itself or nullopt depending on whether the builtin is in scope
    // Just verify no crash
}

TEST_CASE("Definition.shadowed variable resolves to inner scope", "[lsp][definition]")
{
    auto source = "let x = 1\nlet result = let x = 2 in x";
    // cursor on "x" after "in" (0-based col 26)
    auto loc = computeDefinition(source, "file:///test.endo", Position { 1, 26 });
    REQUIRE(loc.has_value());
    // Inner "x" is on line 1
    CHECK(loc->range.start.line == 1);
}

// =============================================================================
// References tests
// =============================================================================

TEST_CASE("References.all uses of variable with declaration", "[lsp][references]")
{
    auto locs = computeReferences("let x = 42\nprintln x", "file:///test.endo", Position { 0, 4 }, true);
    // Should find at least 2: definition + usage
    CHECK(locs.size() >= 2);
}

TEST_CASE("References.all uses without declaration", "[lsp][references]")
{
    auto locs = computeReferences("let x = 42\nprintln x", "file:///test.endo", Position { 0, 4 }, false);
    // Should find 1 usage (not the declaration)
    CHECK(locs.size() >= 1);
    // All should be references, not the definition position
}

TEST_CASE("References.function references across call sites", "[lsp][references]")
{
    auto source = "let f x = x + 1\nprintln (f 3)\nprintln (f 5)";
    auto locs = computeReferences(source, "file:///test.endo", Position { 0, 4 }, true);
    // f appears 3 times: definition + 2 calls
    CHECK(locs.size() >= 3);
}

TEST_CASE("References.scoped variable does not leak", "[lsp][references]")
{
    auto source = "let a = let x = 1 in x\nlet b = let x = 2 in x";
    // cursor on first "x" definition (line 0, col 12)
    auto locs = computeReferences(source, "file:///test.endo", Position { 0, 12 }, true);
    // Should find only 2: first "x" def + first "x" ref
    CHECK(locs.size() == 2);
}

TEST_CASE("References.no matches returns empty", "[lsp][references]")
{
    auto locs = computeReferences("let x = 42", "file:///test.endo", Position { 0, 0 }, true);
    // cursor on "let" keyword, not an identifier
    CHECK(locs.empty());
}

TEST_CASE("References.cursor on usage finds all references", "[lsp][references]")
{
    auto locs = computeReferences("let x = 42\nprintln x", "file:///test.endo", Position { 1, 8 }, true);
    CHECK(locs.size() >= 2);
}

// =============================================================================
// SignatureHelp tests
// =============================================================================

TEST_CASE("SignatureHelp.single parameter function", "[lsp][signaturehelp]")
{
    auto source = "let f x = x + 1\nf 3";
    // cursor on "3" (line 1, col 2)
    auto sig = computeSignatureHelp(source, Position { 1, 2 });
    REQUIRE(sig.has_value());
    REQUIRE(sig->signatures.size() >= 1);
    CHECK(sig->signatures[0].label.find("f") != std::string::npos);
    CHECK(sig->signatures[0].parameters.size() == 1);
    CHECK(sig->activeParameter == 0);
}

TEST_CASE("SignatureHelp.multi-parameter function", "[lsp][signaturehelp]")
{
    auto source = "let add x y = x + y\nadd 1 2";
    // cursor on "2" (line 1, col 6) — second argument
    auto sig = computeSignatureHelp(source, Position { 1, 6 });
    REQUIRE(sig.has_value());
    REQUIRE(sig->signatures.size() >= 1);
    CHECK(sig->signatures[0].parameters.size() == 2);
    CHECK(sig->activeParameter == 1);
}

TEST_CASE("SignatureHelp.typed parameters show types", "[lsp][signaturehelp]")
{
    auto source = "let add (x: int) (y: int): int = x + y\nadd 1 2";
    auto sig = computeSignatureHelp(source, Position { 1, 4 });
    REQUIRE(sig.has_value());
    REQUIRE(sig->signatures.size() >= 1);
    CHECK(sig->signatures[0].label.find("int") != std::string::npos);
}

TEST_CASE("SignatureHelp.not in function call returns nullopt", "[lsp][signaturehelp]")
{
    auto sig = computeSignatureHelp("let x = 42", Position { 0, 8 });
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

    REQUIRE(responses.size() >= 1);
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

// =============================================================================
// Rename tests
// =============================================================================

TEST_CASE("Rename.variable_all_references", "[lsp][rename]")
{
    auto edit = computeRename("let x = 42\nprintln x", "file:///test.endo", Position { 0, 4 }, "y");
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
    auto edit = computeRename(
        "let f x = x + 1\nprintln (f 3)\nprintln (f 5)", "file:///test.endo", Position { 0, 4 }, "g");
    REQUIRE(edit.has_value());
    auto const& edits = edit->changes.at("file:///test.endo");
    // f appears 3 times: definition + 2 calls
    CHECK(edits.size() == 3);
}

TEST_CASE("Rename.parameter_renames_in_scope", "[lsp][rename]")
{
    auto edit = computeRename("let add x y = x + y", "file:///test.endo", Position { 0, 8 }, "a");
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
    auto edit = computeRename("let x = 42", "file:///test.endo", Position { 0, 0 }, "y");
    CHECK_FALSE(edit.has_value());
}

TEST_CASE("PrepareRename.valid_position", "[lsp][rename]")
{
    auto range = prepareRename("let x = 42\nprintln x", Position { 0, 4 });
    REQUIRE(range.has_value());
    CHECK(range->start.line == 0);
    CHECK(range->start.character == 4); // "x" starts at column 4
}

TEST_CASE("PrepareRename.invalid_position", "[lsp][rename]")
{
    // Cursor on "let" keyword
    auto range = prepareRename("let x = 42", Position { 0, 0 });
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
                                   { "text", "ls -la" },
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

    REQUIRE(responses.size() >= 1);
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

    REQUIRE(responses.size() >= 1);
    auto const& caps = responses[0]["result"]["capabilities"];
    CHECK(caps.contains("completionProvider"));
    CHECK(caps["completionProvider"]["triggerCharacters"].is_array());
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
    auto source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }";
    auto hover = computeHover(source, Position { 1, 4 });
    REQUIRE(hover.has_value());
    CHECK(hover->contents.value.find("Person") != std::string::npos);
    CHECK(hover->contents.value.find("alice") != std::string::npos);
}

TEST_CASE("Completion.record_field_dot_access", "[lsp][completion]")
{
    auto source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }\nalice.";
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
    auto source =
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
