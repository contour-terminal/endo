// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>

#include <agent/tools/AgentTool.hpp>

namespace endo::http
{
class HttpClient;
} // namespace endo::http

namespace endo::agent
{

/// Configuration for web search engine selection and API keys.
struct WebSearchConfig
{
    std::string engine = "duckduckgo"; ///< Search engine: "duckduckgo", "brave", or "google"
    std::string apiKey;                ///< API key for Brave or Google (not needed for DuckDuckGo)
    std::string cx;                    ///< Google Custom Search Engine ID
    size_t maxResults = 5;             ///< Maximum number of results to return
};

/// Tool for searching the web via DuckDuckGo (default), Brave, or Google.
///
/// Works out of the box with DuckDuckGo (no API key required).
/// For Brave or Google, configure via shell builtins:
///   set_web_search_engine "brave"
///   set_web_search_api_key "YOUR_KEY"
///
/// Input: { query: string (required) }
/// Returns search results as formatted text with titles, URLs, and descriptions.
class WebSearchTool final: public AgentTool
{
  public:
    /// @brief Constructs a web search tool.
    /// @param httpClient HTTP client for making search requests.
    /// @param config Reference to live configuration (changes are visible immediately).
    WebSearchTool(http::HttpClient const& httpClient, WebSearchConfig const& config);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

  private:
    /// A single search result entry.
    struct SearchResult
    {
        std::string title;
        std::string url;
        std::string description;
    };

    /// @brief Searches DuckDuckGo by parsing the HTML endpoint.
    [[nodiscard]] auto searchDuckDuckGo(std::string const& query, size_t maxResults)
        -> std::expected<std::vector<SearchResult>, ToolError>;

    /// @brief Searches Brave via its JSON API.
    [[nodiscard]] auto searchBrave(std::string const& query, size_t maxResults)
        -> std::expected<std::vector<SearchResult>, ToolError>;

    /// @brief Searches Google via its Custom Search JSON API.
    [[nodiscard]] auto searchGoogle(std::string const& query, size_t maxResults)
        -> std::expected<std::vector<SearchResult>, ToolError>;

    /// @brief Formats search results into a human-readable markdown string.
    [[nodiscard]] static auto formatResults(std::string const& query,
                                            std::vector<SearchResult> const& results) -> std::string;

    /// @brief URL-encodes a string for use in query parameters.
    [[nodiscard]] static auto urlEncode(std::string_view input) -> std::string;

    /// @brief Strips HTML tags from a string.
    [[nodiscard]] static auto stripHtmlTags(std::string_view html) -> std::string;

    /// @brief Decodes common HTML entities (&amp;, &lt;, etc.).
    [[nodiscard]] static auto decodeHtmlEntities(std::string_view text) -> std::string;

    http::HttpClient const& _httpClient;
    WebSearchConfig const& _config;
};

} // namespace endo::agent
