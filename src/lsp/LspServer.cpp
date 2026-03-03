// SPDX-License-Identifier: Apache-2.0
#include "LspServer.hpp"

#include <editor-protocol/JsonTransport.hpp>

#include <iostream>
#include <string>

#include "CompletionProvider.hpp"
#include "DefinitionProvider.hpp"
#include "DiagnosticsProvider.hpp"
#include "DocumentHighlightProvider.hpp"
#include "DocumentSymbolProvider.hpp"
#include "FormattingProvider.hpp"
#include "HoverProvider.hpp"
#include "ReferencesProvider.hpp"
#include "RenameProvider.hpp"
#include "SemanticTokens.hpp"
#include "SignatureHelpProvider.hpp"
#include "StubRuntime.hpp"

namespace endo::lsp
{

using namespace endo::editor_protocol;

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
        else if (method == "textDocument/definition")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleDefinition(params)));
        }
        else if (method == "textDocument/references")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleReferences(params)));
        }
        else if (method == "textDocument/documentHighlight")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleDocumentHighlight(params)));
        }
        else if (method == "textDocument/signatureHelp")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleSignatureHelp(params)));
        }
        else if (method == "textDocument/documentSymbol")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleDocumentSymbol(params)));
        }
        else if (method == "textDocument/rename")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleRename(params)));
        }
        else if (method == "textDocument/prepareRename")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handlePrepareRename(params)));
        }
        else if (method == "textDocument/completion")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleCompletion(params)));
        }
        else if (method == "textDocument/formatting")
        {
            auto const params = message.value("params", nlohmann::json::object());
            writeMessage(_output, makeResponse(id, handleFormatting(params)));
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
              { "definitionProvider", true },
              { "referencesProvider", true },
              { "documentHighlightProvider", true },
              { "signatureHelpProvider",
                nlohmann::json {
                    { "triggerCharacters", nlohmann::json::array({ " ", "(" }) },
                } },
              { "documentSymbolProvider", true },
              { "completionProvider",
                nlohmann::json {
                    { "triggerCharacters", nlohmann::json::array({ ".", "$", " " }) },
                } },
              { "documentFormattingProvider", true },
              { "renameProvider", nlohmann::json { { "prepareProvider", true } } },
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

nlohmann::json LspServer::handleDefinition(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nullptr;

    auto location = computeDefinition(*source, textDoc.uri, position);
    if (!location.has_value())
        return nullptr;

    return *location;
}

nlohmann::json LspServer::handleReferences(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const includeDeclaration =
        params.contains("context") && params["context"].value("includeDeclaration", false);
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto locations = computeReferences(*source, textDoc.uri, position, includeDeclaration);

    auto result = nlohmann::json::array();
    for (auto const& loc: locations)
        result.push_back(loc);
    return result;
}

nlohmann::json LspServer::handleDocumentHighlight(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto highlights = computeDocumentHighlights(*source, position);

    auto result = nlohmann::json::array();
    for (auto const& hl: highlights)
        result.push_back(hl);
    return result;
}

nlohmann::json LspServer::handleSignatureHelp(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nullptr;

    auto sigHelp = computeSignatureHelp(*source, position);
    if (!sigHelp.has_value())
        return nullptr;

    return *sigHelp;
}

nlohmann::json LspServer::handleDocumentSymbol(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto symbols = computeDocumentSymbols(*source);

    auto result = nlohmann::json::array();
    for (auto const& sym: symbols)
        result.push_back(sym);
    return result;
}

nlohmann::json LspServer::handleRename(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const newName = params.at("newName").get<std::string>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nullptr;

    auto edit = computeRename(*source, textDoc.uri, position, newName);
    if (!edit.has_value())
        return nullptr;

    return *edit;
}

nlohmann::json LspServer::handlePrepareRename(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nullptr;

    auto range = prepareRename(*source, position);
    if (!range.has_value())
        return nullptr;

    return *range;
}

nlohmann::json LspServer::handleCompletion(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    return computeCompletion(*source, position);
}

nlohmann::json LspServer::handleFormatting(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto edits = computeFormatting(*source);

    auto result = nlohmann::json::array();
    for (auto const& edit: edits)
        result.push_back(edit);
    return result;
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
