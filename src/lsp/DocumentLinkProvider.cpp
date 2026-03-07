// SPDX-License-Identifier: Apache-2.0
#include "DocumentLinkProvider.hpp"

#include <endo-language/lexer/Lexer.hpp>

#include <filesystem>
#include <string>

namespace endo::lsp
{

namespace
{
    /// Extracts the directory part of a file:// URI.
    [[nodiscard]] std::string uriToDirectory(std::string const& uri)
    {
        auto path = uri;
        // Remove file:// prefix
        if (path.starts_with("file://"))
            path = path.substr(7);
        // Get directory
        auto const lastSlash = path.rfind('/');
        if (lastSlash != std::string::npos)
            return path.substr(0, lastSlash);
        return ".";
    }

    /// Resolves a path relative to a base directory and returns a file:// URI.
    [[nodiscard]] std::string resolveToFileUri(std::string const& basePath, std::string const& relativePath)
    {
        auto resolved = std::filesystem::path(basePath) / relativePath;
        return "file://" + resolved.lexically_normal().string();
    }
} // namespace

std::vector<DocumentLink> computeDocumentLinks(std::string const& source, std::string const& uri)
{
    std::vector<DocumentLink> links;

    auto const baseDir = uriToDirectory(uri);

    // Simple line-based scan for source commands and File.open calls
    auto lexer = Lexer { std::make_unique<StringSource>(source) };
    auto prevToken = Token::EndOfInput;
    auto prevPrevLiteral = std::string {};
    auto prevLiteral = std::string {};

    while (lexer.currentToken() != Token::EndOfInput)
    {
        auto const token = lexer.currentToken();
        auto const literal = lexer.currentLiteral();
        auto const range = lexer.currentRange();

        // Match: source "filepath"
        if (token == Token::String && prevToken == Token::Identifier && prevLiteral == "source")
        {
            links.push_back(DocumentLink {
                .range = toRange(range),
                .target = resolveToFileUri(baseDir, literal),
                .tooltip = "Open " + literal,
            });
        }

        // Match: File.open "filepath" (where prevPrevLiteral=open, prevLiteral=<string>)
        // Actually we look for String after "open" in a File.open context
        if (token == Token::String && prevToken == Token::Identifier && prevLiteral == "open")
        {
            links.push_back(DocumentLink {
                .range = toRange(range),
                .target = resolveToFileUri(baseDir, literal),
                .tooltip = "Open " + literal,
            });
        }

        prevPrevLiteral = prevLiteral;
        prevLiteral = literal;
        prevToken = token;
        lexer.nextToken();
    }

    return links;
}

DocumentLink resolveDocumentLink(DocumentLink link)
{
    // All links are currently eagerly resolved, so just return as-is
    if (!link.target.has_value() && link.data.has_value())
    {
        // If we had deferred resolution, we'd resolve the data here
        if (link.data->contains("path"))
            link.target = link.data->at("path").get<std::string>();
    }
    return link;
}

} // namespace endo::lsp
