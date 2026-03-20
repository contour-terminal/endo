// SPDX-License-Identifier: Apache-2.0
#include "LspServer.hpp"

#include <editor-protocol/JsonTransport.hpp>
#include <editor-protocol/StubRuntime.hpp>

#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

#include "CallHierarchyProvider.hpp"
#include "CodeActionProvider.hpp"
#include "CodeLensProvider.hpp"
#include "CompletionProvider.hpp"
#include "DefinitionProvider.hpp"
#include "DiagnosticsProvider.hpp"
#include "DocumentHighlightProvider.hpp"
#include "DocumentLinkProvider.hpp"
#include "DocumentSymbolProvider.hpp"
#include "FoldingRangeProvider.hpp"
#include "FormattingProvider.hpp"
#include "HoverProvider.hpp"
#include "InlayHintProvider.hpp"
#include "InlineValueProvider.hpp"
#include "OnTypeFormattingProvider.hpp"
#include "RangeFormattingProvider.hpp"
#include "ReferencesProvider.hpp"
#include "RenameProvider.hpp"
#include "SelectionRangeProvider.hpp"
#include "SemanticTokens.hpp"
#include "SignatureHelpProvider.hpp"
#include "TypeDefinitionProvider.hpp"
#include "WorkspaceSymbolProvider.hpp"

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

    try
    {
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
            // Tier 1
            else if (method == "textDocument/semanticTokens/full")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleSemanticTokensFull(params)));
            }
            else if (method == "textDocument/semanticTokens/range")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleSemanticTokensRange(params)));
            }
            else if (method == "textDocument/semanticTokens/full/delta")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleSemanticTokensFullDelta(params)));
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
            else if (method == "completionItem/resolve")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCompletionResolve(params)));
            }
            else if (method == "textDocument/formatting")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleFormatting(params)));
            }
            else if (method == "textDocument/rangeFormatting")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleRangeFormatting(params)));
            }
            else if (method == "textDocument/onTypeFormatting")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleOnTypeFormatting(params)));
            }
            // Tier 2
            else if (method == "textDocument/inlayHint")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleInlayHint(params)));
            }
            else if (method == "inlayHint/resolve")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleInlayHintResolve(params)));
            }
            else if (method == "textDocument/foldingRange")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleFoldingRange(params)));
            }
            else if (method == "textDocument/selectionRange")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleSelectionRange(params)));
            }
            else if (method == "textDocument/codeAction")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCodeAction(params)));
            }
            else if (method == "codeAction/resolve")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCodeActionResolve(params)));
            }
            // Tier 3
            else if (method == "workspace/symbol")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleWorkspaceSymbol(params)));
            }
            else if (method == "textDocument/prepareCallHierarchy")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handlePrepareCallHierarchy(params)));
            }
            else if (method == "callHierarchy/incomingCalls")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCallHierarchyIncomingCalls(params)));
            }
            else if (method == "callHierarchy/outgoingCalls")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCallHierarchyOutgoingCalls(params)));
            }
            else if (method == "textDocument/typeDefinition")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleTypeDefinition(params)));
            }
            else if (method == "textDocument/documentLink")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleDocumentLink(params)));
            }
            else if (method == "documentLink/resolve")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleDocumentLinkResolve(params)));
            }
            // Tier 4
            else if (method == "textDocument/codeLens")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCodeLens(params)));
            }
            else if (method == "codeLens/resolve")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleCodeLensResolve(params)));
            }
            else if (method == "textDocument/inlineValue")
            {
                auto const params = message.value("params", nlohmann::json::object());
                writeMessage(_output, makeResponse(id, handleInlineValue(params)));
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
            else if (method == "window/workDoneProgress/cancel")
            {
                // Acknowledge cancellation — no-op for now
            }
            // Unknown notifications are silently ignored per LSP spec
        }
    }
    catch (std::exception const& ex)
    {
        if (hasId && !method.empty())
            writeMessage(
                _output,
                makeErrorResponse(id, ErrorCode::InternalError, std::string("Internal error: ") + ex.what()));
        // Notifications: silently ignore exceptions per LSP spec
    }
    catch (...)
    {
        if (hasId && !method.empty())
            writeMessage(_output,
                         makeErrorResponse(id, ErrorCode::InternalError, "Internal error (unknown exception)"));
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
                    { "resolveProvider", true },
                } },
              { "documentFormattingProvider", true },
              { "documentRangeFormattingProvider", true },
              { "documentOnTypeFormattingProvider",
                nlohmann::json {
                    { "firstTriggerCharacter", "\n" },
                    { "moreTriggerCharacter", nlohmann::json::array({ "|" }) },
                } },
              { "inlayHintProvider", nlohmann::json { { "resolveProvider", true } } },
              { "foldingRangeProvider", true },
              { "selectionRangeProvider", true },
              { "codeActionProvider",
                nlohmann::json {
                    { "codeActionKinds", nlohmann::json::array({ "quickfix" }) },
                    { "resolveProvider", true },
                } },
              { "renameProvider", nlohmann::json { { "prepareProvider", true } } },
              { "semanticTokensProvider",
                nlohmann::json {
                    { "legend", legend },
                    { "full", nlohmann::json { { "delta", true } } },
                    { "range", true },
                } },
              { "workspaceSymbolProvider", true },
              { "callHierarchyProvider", true },
              { "typeDefinitionProvider", true },
              { "documentLinkProvider", nlohmann::json { { "resolveProvider", true } } },
              { "codeLensProvider", nlohmann::json { { "resolveProvider", true } } },
              { "inlineValueProvider", true },
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
    _semanticTokenCache.erase(textDoc.uri);

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

    // Assign resultId and cache for delta support
    auto const resultId = std::to_string(_nextResultId++);
    tokens.resultId = resultId;
    _semanticTokenCache[textDoc.uri] = SemanticTokensCacheEntry {
        .resultId = resultId,
        .data = tokens.data,
    };

    return tokens;
}

nlohmann::json LspServer::handleSemanticTokensRange(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const range = params.at("range").get<Range>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json { { "data", nlohmann::json::array() } };

    auto const fullTokens = computeSemanticTokens(*source);

    // Filter tokens by range and re-delta-encode
    SemanticTokens rangeTokens;
    auto prevLine = 0;
    auto prevChar = 0;
    auto absLine = 0;
    auto absChar = 0;

    for (auto i = size_t { 0 }; i + 4 < fullTokens.data.size(); i += 5)
    {
        auto const deltaLine = fullTokens.data[i];
        auto const deltaStart = fullTokens.data[i + 1];
        auto const length = fullTokens.data[i + 2];
        auto const tokenType = fullTokens.data[i + 3];
        auto const tokenMods = fullTokens.data[i + 4];

        absLine += deltaLine;
        absChar = (deltaLine == 0) ? (absChar + deltaStart) : deltaStart;

        // Check if token is within range
        if (absLine < range.start.line)
            continue;
        if (absLine > range.end.line)
            break;

        auto const newDeltaLine = absLine - prevLine;
        auto const newDeltaStart = (newDeltaLine == 0) ? (absChar - prevChar) : absChar;

        rangeTokens.data.push_back(newDeltaLine);
        rangeTokens.data.push_back(newDeltaStart);
        rangeTokens.data.push_back(length);
        rangeTokens.data.push_back(tokenType);
        rangeTokens.data.push_back(tokenMods);

        prevLine = absLine;
        prevChar = absChar;
    }

    return rangeTokens;
}

nlohmann::json LspServer::handleSemanticTokensFullDelta(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const previousResultId = params.value("previousResultId", std::string {});
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json { { "data", nlohmann::json::array() } };

    // Save old data before overwriting cache
    auto const cacheIt = _semanticTokenCache.find(textDoc.uri);
    auto const cacheHit =
        cacheIt != _semanticTokenCache.end() && cacheIt->second.resultId == previousResultId;
    auto oldData = cacheHit ? cacheIt->second.data : std::vector<int> {};

    // Compute new tokens
    auto const newTokens = computeSemanticTokens(*source);
    auto const newResultId = std::to_string(_nextResultId++);

    // Update cache
    _semanticTokenCache[textDoc.uri] = SemanticTokensCacheEntry {
        .resultId = newResultId,
        .data = newTokens.data,
    };

    if (!cacheHit)
    {
        // Cache miss — return full tokens
        auto result = SemanticTokens { .resultId = newResultId, .data = newTokens.data };
        return result;
    }

    auto const& newData = newTokens.data;

    // Compute diff
    SemanticTokensDelta delta;
    delta.resultId = newResultId;

    if (oldData != newData)
    {
        // Simple diff: find first and last differing positions
        auto const minSize = std::min(oldData.size(), newData.size());
        auto firstDiff = size_t { 0 };
        while (firstDiff < minSize && oldData[firstDiff] == newData[firstDiff])
            ++firstDiff;

        auto oldEnd = oldData.size();
        auto newEnd = newData.size();
        while (oldEnd > firstDiff && newEnd > firstDiff && oldData[oldEnd - 1] == newData[newEnd - 1])
        {
            --oldEnd;
            --newEnd;
        }

        auto edit = SemanticTokensEdit {
            .start = static_cast<int>(firstDiff),
            .deleteCount = static_cast<int>(oldEnd - firstDiff),
        };
        for (auto i = firstDiff; i < newEnd; ++i)
            edit.data.push_back(newData[i]);

        delta.edits.push_back(std::move(edit));
    }

    return delta;
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
    if (!params.contains("textDocument") || !params["textDocument"].contains("uri"))
        throw std::invalid_argument("missing textDocument.uri");
    if (!params.contains("position"))
        throw std::invalid_argument("missing position");
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

nlohmann::json LspServer::handleCompletionResolve(nlohmann::json const& params)
{
    // Params IS the completion item itself for resolve
    auto result = params;

    // If item has data with a "kind" field, we can enrich the documentation
    if (params.contains("data") && params["data"].contains("kind"))
    {
        auto const kind = params["data"]["kind"].get<std::string>();
        if (kind == "builtin" && params.contains("label"))
        {
            auto const label = params["label"].get<std::string>();
            result["documentation"] = nlohmann::json {
                { "kind", "markdown" },
                { "value", std::format("Built-in function `{}`", label) },
            };
        }
        else if (kind == "keyword" && params.contains("label"))
        {
            auto const label = params["label"].get<std::string>();
            result["documentation"] = nlohmann::json {
                { "kind", "markdown" },
                { "value", std::format("Keyword `{}`", label) },
            };
        }
    }

    return result;
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

nlohmann::json LspServer::handleRangeFormatting(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const range = params.at("range").get<Range>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto edits = computeRangeFormatting(*source, range);

    auto result = nlohmann::json::array();
    for (auto const& edit: edits)
        result.push_back(edit);
    return result;
}

nlohmann::json LspServer::handleOnTypeFormatting(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const ch = params.at("ch").get<std::string>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto edits = computeOnTypeFormatting(*source, position, ch);

    auto result = nlohmann::json::array();
    for (auto const& edit: edits)
        result.push_back(edit);
    return result;
}

nlohmann::json LspServer::handleInlayHint(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const range = params.at("range").get<Range>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto hints = computeInlayHints(*source, range);

    auto result = nlohmann::json::array();
    for (auto const& hint: hints)
        result.push_back(hint);
    return result;
}

nlohmann::json LspServer::handleInlayHintResolve(nlohmann::json const& params)
{
    // Params IS the inlay hint itself
    auto result = params;

    // Add tooltip with expanded type information
    if (params.contains("data") && params["data"].contains("type"))
    {
        auto const type = params["data"]["type"].get<std::string>();
        result["tooltip"] = nlohmann::json {
            { "kind", "markdown" },
            { "value", std::format("Type: `{}`", type) },
        };
    }

    return result;
}

nlohmann::json LspServer::handleFoldingRange(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto ranges = computeFoldingRanges(*source);

    auto result = nlohmann::json::array();
    for (auto const& r: ranges)
        result.push_back(r);
    return result;
}

nlohmann::json LspServer::handleSelectionRange(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto positions = params.at("positions").get<std::vector<Position>>();
    auto ranges = computeSelectionRanges(*source, positions);

    auto result = nlohmann::json::array();
    for (auto const& r: ranges)
        result.push_back(r);
    return result;
}

nlohmann::json LspServer::handleCodeAction(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const range = params.at("range").get<Range>();
    auto const context = params.at("context").get<CodeActionContext>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto actions = computeCodeActions(*source, textDoc.uri, range, context.diagnostics);

    auto result = nlohmann::json::array();
    for (auto const& action: actions)
        result.push_back(action);
    return result;
}

nlohmann::json LspServer::handleCodeActionResolve(nlohmann::json const& params)
{
    // All code actions are eagerly resolved, so just return as-is
    return params;
}

nlohmann::json LspServer::handleWorkspaceSymbol(nlohmann::json const& params)
{
    auto const query = params.value("query", std::string {});

    auto symbols = computeWorkspaceSymbols(_documents, query);

    auto result = nlohmann::json::array();
    for (auto const& sym: symbols)
        result.push_back(sym);
    return result;
}

nlohmann::json LspServer::handlePrepareCallHierarchy(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto items = prepareCallHierarchy(*source, textDoc.uri, position);

    auto result = nlohmann::json::array();
    for (auto const& item: items)
        result.push_back(item);
    return result;
}

nlohmann::json LspServer::handleCallHierarchyIncomingCalls(nlohmann::json const& params)
{
    auto const item = params.at("item").get<CallHierarchyItem>();
    auto const* source = _documents.get(item.uri);
    if (!source)
        return nlohmann::json::array();

    auto calls = computeIncomingCalls(*source, item.uri, item);

    auto result = nlohmann::json::array();
    for (auto const& call: calls)
        result.push_back(call);
    return result;
}

nlohmann::json LspServer::handleCallHierarchyOutgoingCalls(nlohmann::json const& params)
{
    auto const item = params.at("item").get<CallHierarchyItem>();
    auto const* source = _documents.get(item.uri);
    if (!source)
        return nlohmann::json::array();

    auto calls = computeOutgoingCalls(*source, item.uri, item);

    auto result = nlohmann::json::array();
    for (auto const& call: calls)
        result.push_back(call);
    return result;
}

nlohmann::json LspServer::handleTypeDefinition(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const position = params.at("position").get<Position>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nullptr;

    auto location = computeTypeDefinition(*source, textDoc.uri, position);
    if (!location.has_value())
        return nullptr;

    return *location;
}

nlohmann::json LspServer::handleDocumentLink(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto links = computeDocumentLinks(*source, textDoc.uri);

    auto result = nlohmann::json::array();
    for (auto const& link: links)
        result.push_back(link);
    return result;
}

nlohmann::json LspServer::handleDocumentLinkResolve(nlohmann::json const& params)
{
    auto link = params.get<DocumentLink>();
    link = resolveDocumentLink(std::move(link));
    return link;
}

nlohmann::json LspServer::handleCodeLens(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto lenses = computeCodeLenses(*source, textDoc.uri);

    auto result = nlohmann::json::array();
    for (auto const& lens: lenses)
        result.push_back(lens);
    return result;
}

nlohmann::json LspServer::handleCodeLensResolve(nlohmann::json const& params)
{
    auto lens = params.get<CodeLens>();

    // Need the source to count references
    if (lens.data.has_value() && lens.data->contains("uri"))
    {
        auto const uri = lens.data->at("uri").get<std::string>();
        auto const* source = _documents.get(uri);
        if (source)
            lens = resolveCodeLens(*source, std::move(lens));
    }

    return lens;
}

nlohmann::json LspServer::handleInlineValue(nlohmann::json const& params)
{
    auto const textDoc = params.at("textDocument").get<TextDocumentIdentifier>();
    auto const range = params.at("range").get<Range>();
    auto const* source = _documents.get(textDoc.uri);
    if (!source)
        return nlohmann::json::array();

    auto values = computeInlineValues(*source, range);

    auto result = nlohmann::json::array();
    for (auto const& val: values)
        result.push_back(val);
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

void LspServer::showMessage(MessageType type, std::string const& message)
{
    writeMessage(_output,
                 makeNotification("window/showMessage",
                                  nlohmann::json {
                                      { "type", static_cast<int>(type) },
                                      { "message", message },
                                  }));
}

void LspServer::logMessage(MessageType type, std::string const& message)
{
    writeMessage(_output,
                 makeNotification("window/logMessage",
                                  nlohmann::json {
                                      { "type", static_cast<int>(type) },
                                      { "message", message },
                                  }));
}

} // namespace endo::lsp
