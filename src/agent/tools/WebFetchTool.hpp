// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

#include <agent/tools/AgentTool.hpp>

namespace endo::http
{
class HttpClient;
} // namespace endo::http

namespace endo::agent
{

/// Configuration for the web fetch tool.
struct WebFetchConfig
{
    size_t maxContentSize = 512 * 1024;         ///< Max response body size (512 KB).
    std::chrono::seconds requestTimeout { 30 }; ///< HTTP request timeout.
    std::chrono::minutes cacheTtl { 15 };       ///< In-memory URL cache TTL.
    size_t maxOutputLength = 30'000;            ///< Default max_length for returned content.
};

/// Tool for fetching web page content and converting it to readable text.
///
/// Input: { url: string (required), max_length?: int }
/// - url: URL to fetch (http:// auto-upgraded to https://).
/// - max_length: Max characters in returned content (default: 30000).
///
/// Converts HTML to markdown, pretty-prints JSON, and returns other content as-is.
/// Includes a 15-minute in-memory cache.
class WebFetchTool final: public AgentTool
{
  public:
    /// @brief Constructs a web fetch tool.
    /// @param httpClient HTTP client for making requests.
    /// @param config Configuration for timeouts, limits, and caching.
    WebFetchTool(http::HttpClient const& httpClient, WebFetchConfig const& config);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

    [[nodiscard]] auto riskLevel() const noexcept -> ToolRisk override { return ToolRisk::ReadOnly; }

    /// @brief Converts HTML content to simplified markdown.
    /// @param html The HTML source to convert.
    /// @return Markdown-formatted text.
    [[nodiscard]] static auto htmlToMarkdown(std::string_view html) -> std::string;

  private:
    /// @brief Extracts content type from HTTP response headers.
    /// @param headers The response headers.
    /// @return The content type string (e.g., "text/html"), or empty if not found.
    [[nodiscard]] static auto extractContentType(std::vector<std::string> const& headers) -> std::string;

    /// A cached fetch result.
    struct CacheEntry
    {
        std::string content;
        std::chrono::steady_clock::time_point expiry;
    };

    http::HttpClient const& _httpClient;
    WebFetchConfig const& _config;
    std::unordered_map<std::string, CacheEntry> _cache;
};

} // namespace endo::agent
