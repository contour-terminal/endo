// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "DiagnosticsProvider.hpp"
#include "DocumentStore.hpp"
#include "HoverProvider.hpp"
#include "JsonRpc.hpp"
#include "LspServer.hpp"
#include "SemanticTokens.hpp"
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
