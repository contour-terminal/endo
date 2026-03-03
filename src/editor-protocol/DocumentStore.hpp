// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>

namespace endo::editor_protocol
{

/// Manages open documents indexed by URI.
///
/// Provides full text synchronization (TextDocumentSyncKind.Full).
class DocumentStore
{
  public:
    /// Registers a newly opened document.
    /// @param uri Document URI
    /// @param text Full document text
    /// @param version Document version
    void open(std::string const& uri, std::string text, int version);

    /// Replaces the entire content of an open document.
    /// @param uri Document URI
    /// @param text New full document text
    /// @param version New document version
    void update(std::string const& uri, std::string text, int version);

    /// Removes a document from the store.
    /// @param uri Document URI to close
    void close(std::string const& uri);

    /// Retrieves the text of an open document.
    /// @param uri Document URI
    /// @return Pointer to the document text, or nullptr if not found
    [[nodiscard]] std::string const* get(std::string const& uri) const;

    /// Retrieves the version of an open document.
    /// @param uri Document URI
    /// @return Document version, or -1 if not found
    [[nodiscard]] int version(std::string const& uri) const;

  private:
    struct Document
    {
        std::string text;
        int version = 0;
    };

    std::unordered_map<std::string, Document> _documents;
};

} // namespace endo::editor_protocol
