// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <istream>
#include <ostream>
#include <string>

#include "DocumentStore.hpp"
#include "LspTypes.hpp"

namespace endo::lsp
{

/// LSP server implementing the Language Server Protocol for Endo shell scripts.
///
/// Communicates via JSON-RPC 2.0 over stdin/stdout (or injected streams for testing).
/// Supports document synchronization, diagnostics, semantic tokens, and hover.
class LspServer
{
  public:
    /// Constructs the server with the given I/O streams.
    /// @param input Input stream for reading JSON-RPC messages (default: std::cin)
    /// @param output Output stream for writing JSON-RPC messages (default: std::cout)
    explicit LspServer(std::istream& input, std::ostream& output);

    /// Default constructor using std::cin and std::cout.
    LspServer();

    /// Runs the main message loop until exit.
    /// @return 0 on clean exit (shutdown then exit), 1 on unclean exit
    int run();

  private:
    // Lifecycle
    [[nodiscard]] nlohmann::json handleInitialize(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleShutdown();
    void handleExit();

    // Document synchronization
    void handleDidOpen(nlohmann::json const& params);
    void handleDidChange(nlohmann::json const& params);
    void handleDidClose(nlohmann::json const& params);

    // Language features
    [[nodiscard]] nlohmann::json handleSemanticTokensFull(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleHover(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleDefinition(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleReferences(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleSignatureHelp(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleDocumentSymbol(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleRename(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handlePrepareRename(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleCompletion(nlohmann::json const& params);
    [[nodiscard]] nlohmann::json handleFormatting(nlohmann::json const& params);

    // Notifications
    void publishDiagnostics(std::string const& uri);

    // Message dispatch
    void dispatch(nlohmann::json const& message);

    std::istream& _input;
    std::ostream& _output;
    DocumentStore _documents;
    CoreVM::Runtime _runtime;
    bool _initialized = false;
    bool _shutdownRequested = false;
    bool _exitRequested = false;
};

} // namespace endo::lsp
