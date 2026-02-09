// SPDX-License-Identifier: Apache-2.0
#include "LspServer.hpp"

#include <iostream>
#include <string>

#include "DiagnosticsProvider.hpp"
#include "HoverProvider.hpp"
#include "JsonRpc.hpp"
#include "SemanticTokens.hpp"

namespace endo::lsp
{

namespace
{

    /// Registers the minimal runtime builtins needed for the parser.
    /// Follows the TestRuntime pattern from TestHelper.cpp.
    void registerStubRuntime(CoreVM::Runtime& runtime)
    {
        // Dummy handler that does nothing
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
    }

} // namespace

LspServer::LspServer(std::istream& input, std::ostream& output): _input(input), _output(output)
{
    registerStubRuntime(_runtime);
}

LspServer::LspServer(): LspServer(std::cin, std::cout)
{
}

int LspServer::run()
{
    while (!_exitRequested)
    {
        auto message = readMessage(_input);
        if (!message.has_value())
        {
            // Connection closed or read error
            break;
        }
        dispatch(*message);
    }

    return _shutdownRequested ? 0 : 1;
}

void LspServer::dispatch(nlohmann::json const& message)
{
    auto const hasId = message.contains("id");
    auto const method = message.value("method", std::string {});
    auto const id = hasId ? message["id"] : nlohmann::json {};

    // Requests (have id and method)
    if (hasId && !method.empty())
    {
        // Before initialize, only allow "initialize"
        if (!_initialized && method != "initialize")
        {
            writeMessage(
                _output,
                makeErrorResponse(id, ErrorCode::ServerNotInitialized, "Server not yet initialized"));
            return;
        }

        if (method == "initialize")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleInitialize(params)));
        }
        else if (method == "shutdown")
        {
            writeMessage(_output, makeResponse(id, handleShutdown()));
        }
        else if (method == "textDocument/semanticTokens/full")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleSemanticTokensFull(params)));
        }
        else if (method == "textDocument/hover")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleHover(params)));
        }
        else
        {
            writeMessage(_output,
                         makeErrorResponse(id, ErrorCode::MethodNotFound, "Method not found: " + method));
        }
    }
    // Notifications (have method but no id)
    else if (!method.empty())
    {
        if (method == "initialized")
        {
            // No-op, server is already initialized
        }
        else if (method == "exit")
        {
            handleExit();
        }
        else if (method == "textDocument/didOpen")
        {
            auto const params = message.value("params", nlohmann::json::object());
            handleDidOpen(params);
        }
        else if (method == "textDocument/didChange")
        {
            auto const params = message.value("params", nlohmann::json::object());
            handleDidChange(params);
        }
        else if (method == "textDocument/didClose")
        {
            auto const params = message.value("params", nlohmann::json::object());
            handleDidClose(params);
        }
        // Unknown notifications are silently ignored per LSP spec
    }
}

nlohmann::json LspServer::handleInitialize(nlohmann::json const& /*params*/)
{
    _initialized = true;

    auto legend = createSemanticTokensLegend();

    return nlohmann::json {
        { "capabilities",
          nlohmann::json {
              { "textDocumentSync", 1 }, // Full sync
              { "hoverProvider", true },
              { "semanticTokensProvider",
                nlohmann::json {
                    { "legend", legend },
                    { "full", true },
                } },
          } },
        { "serverInfo",
          nlohmann::json {
              { "name", "endo-lsp" },
              { "version", "0.1.0" },
          } },
    };
}

nlohmann::json LspServer::handleShutdown()
{
    _shutdownRequested = true;
    return nlohmann::json {};
}

void LspServer::handleExit()
{
    _exitRequested = true;
}

void LspServer::handleDidOpen(nlohmann::json const& params)
{
    auto const doc = params.at("textDocument").get<TextDocumentItem>();
    _documents.open(doc.uri, doc.text, doc.version);
    publishDiagnostics(doc.uri);
}

void LspServer::handleDidChange(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<VersionedTextDocumentIdentifier>();
    auto const& contentChanges = params.at("contentChanges");
    if (!contentChanges.empty())
    {
        auto const change = contentChanges[0].get<TextDocumentContentChangeEvent>();
        _documents.update(textDoc.uri, change.text, textDoc.version);
    }
    publishDiagnostics(textDoc.uri);
}

void LspServer::handleDidClose(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    _documents.close(textDoc.uri);

    // Clear diagnostics for closed document
    writeMessage(_output,
                 makeNotification("textDocument/publishDiagnostics",
                                  nlohmann::json {
                                      { "uri", textDoc.uri },
                                      { "diagnostics", nlohmann::json::array() },
                                  }));
}

nlohmann::json LspServer::handleSemanticTokensFull(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json { { "data", nlohmann::json::array() } };

    auto tokens = computeSemanticTokens(*source);
    return tokens;
}

nlohmann::json LspServer::handleHover(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nullptr;

    auto hover = computeHover(*source, position);
    if (!hover.has_value())
        return nullptr;

    return *hover;
}

void LspServer::publishDiagnostics(std::string const& uri)
{
    auto const* source = _documents.get(uri);
    if (!source)
        return;

    auto diagnostics = computeDiagnostics(*source, uri, _runtime);

    nlohmann::json diagArray = nlohmann::json::array();
    for (auto const& diag: diagnostics)
        diagArray.push_back(diag);

    writeMessage(_output,
                 makeNotification("textDocument/publishDiagnostics",
                                  nlohmann::json {
                                      { "uri", uri },
                                      { "diagnostics", diagArray },
                                  }));
}

} // namespace endo::lsp
