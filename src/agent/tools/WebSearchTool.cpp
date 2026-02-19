// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <cctype>
#include <format>
#include <string>

#include <agent/tools/WebSearchTool.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto DuckDuckGoUrl = "https://html.duckduckgo.com/html/";
    constexpr auto BraveSearchUrl = "https://api.search.brave.com/res/v1/web/search";
    constexpr auto GoogleSearchUrl = "https://www.googleapis.com/customsearch/v1";
} // namespace

WebSearchTool::WebSearchTool(http::HttpClient const& httpClient, WebSearchConfig const& config):
    _httpClient(httpClient), _config(config)
{
}

auto WebSearchTool::name() const noexcept -> std::string_view
{
    return "web_search";
}

auto WebSearchTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "web_search",
        .description = "Searches the web and returns results with titles, URLs, and descriptions. "
                       "Use this to find current information, documentation, or answers to questions.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "query",
                      { { "type", "string" }, { "description", "The search query to execute" } } } } },
                { "required", nlohmann::json::array({ "query" }) },
            },
    };
}

auto WebSearchTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const query = arguments.value("query", std::string {});
    if (query.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: query" });

    auto results = std::expected<std::vector<SearchResult>, ToolError> {};

    if (_config.engine == "brave")
        results = searchBrave(query, _config.maxResults);
    else if (_config.engine == "google")
        results = searchGoogle(query, _config.maxResults);
    else
        results = searchDuckDuckGo(query, _config.maxResults);

    if (!results.has_value())
        return std::unexpected(results.error());

    if (results->empty())
        return ToolResult { .content = std::format("No results found for \"{}\".", query), .isError = false };

    return ToolResult {
        .content = formatResults(query, *results),
        .isError = false,
    };
}

auto WebSearchTool::searchDuckDuckGo(std::string const& query, size_t maxResults)
    -> std::expected<std::vector<SearchResult>, ToolError>
{
    auto const url = std::format("{}?q={}", DuckDuckGoUrl, urlEncode(query));

    auto request = http::HttpRequest {};
    request.url = url;
    request.method = http::HttpMethod::Get;
    request.headers = { "User-Agent: Mozilla/5.0 (compatible; EndoShell/1.0)" };
    request.timeout = std::chrono::seconds { 15 };

    auto response = _httpClient.execute(request);
    if (!response.has_value())
        return std::unexpected(
            ToolError { .message = std::format("DuckDuckGo request failed: {}", response.error().message) });

    if (response->statusCode != 200)
        return std::unexpected(
            ToolError { .message = std::format("DuckDuckGo returned HTTP {}", response->statusCode) });

    auto results = std::vector<SearchResult> {};
    auto const& body = response->body;

    // Parse DuckDuckGo HTML results.
    // Results are in <a class="result__a"> for title/URL and <a class="result__snippet"> for description.
    auto pos = size_t { 0 };
    while (results.size() < maxResults)
    {
        // Find the result link
        auto const linkMarker = std::string { "class=\"result__a\"" };
        auto linkPos = body.find(linkMarker, pos);
        if (linkPos == std::string::npos)
            break;

        // Extract href from the anchor tag
        auto hrefPos = body.rfind("href=\"", linkPos);
        if (hrefPos == std::string::npos || hrefPos < pos)
        {
            pos = linkPos + linkMarker.size();
            continue;
        }
        hrefPos += 6; // skip past href="
        auto const hrefEnd = body.find('"', hrefPos);
        if (hrefEnd == std::string::npos)
            break;

        auto resultUrl = body.substr(hrefPos, hrefEnd - hrefPos);

        // Extract title text between > and </a>
        auto const titleStart = body.find('>', linkPos);
        if (titleStart == std::string::npos)
            break;
        auto const titleEnd = body.find("</a>", titleStart);
        if (titleEnd == std::string::npos)
            break;

        auto title = stripHtmlTags(body.substr(titleStart + 1, titleEnd - titleStart - 1));
        title = decodeHtmlEntities(title);

        // Find the snippet
        auto description = std::string {};
        auto const snippetMarker = std::string { "class=\"result__snippet\"" };
        auto snippetPos = body.find(snippetMarker, titleEnd);
        if (snippetPos != std::string::npos)
        {
            auto const snippetStart = body.find('>', snippetPos);
            if (snippetStart != std::string::npos)
            {
                auto const snippetEnd = body.find("</a>", snippetStart);
                if (snippetEnd != std::string::npos)
                {
                    description = stripHtmlTags(body.substr(snippetStart + 1, snippetEnd - snippetStart - 1));
                    description = decodeHtmlEntities(description);
                }
            }
            pos = snippetPos + snippetMarker.size();
        }
        else
        {
            pos = titleEnd + 4;
        }

        // DDG tracking URLs redirect through //duckduckgo.com/l/?uddg=<actual_url>
        // Extract the actual URL if present
        if (auto const uddgPos = resultUrl.find("uddg="); uddgPos != std::string::npos)
        {
            auto encoded = resultUrl.substr(uddgPos + 5);
            // URL-decode the actual URL (percent-decode)
            auto decoded = std::string {};
            for (size_t i = 0; i < encoded.size(); ++i)
            {
                if (encoded[i] == '%' && i + 2 < encoded.size())
                {
                    auto hex = encoded.substr(i + 1, 2);
                    auto const ch = static_cast<char>(std::stoi(hex, nullptr, 16));
                    decoded += ch;
                    i += 2;
                }
                else if (encoded[i] == '&')
                {
                    break; // Stop at next query parameter
                }
                else
                {
                    decoded += encoded[i];
                }
            }
            resultUrl = std::move(decoded);
        }

        if (!title.empty() && !resultUrl.empty())
            results.push_back(SearchResult { .title = std::move(title),
                                             .url = std::move(resultUrl),
                                             .description = std::move(description) });
    }

    return results;
}

auto WebSearchTool::searchBrave(std::string const& query, size_t maxResults)
    -> std::expected<std::vector<SearchResult>, ToolError>
{
    if (_config.apiKey.empty())
        return std::unexpected(ToolError { .message = "Brave search requires an API key. "
                                                      "Set it with: set_web_search_api_key \"YOUR_KEY\"" });

    auto const url = std::format("{}?q={}&count={}", BraveSearchUrl, urlEncode(query), maxResults);

    auto request = http::HttpRequest {};
    request.url = url;
    request.method = http::HttpMethod::Get;
    request.headers = { std::format("X-Subscription-Token: {}", _config.apiKey), "Accept: application/json" };
    request.timeout = std::chrono::seconds { 15 };

    auto response = _httpClient.execute(request);
    if (!response.has_value())
        return std::unexpected(
            ToolError { .message = std::format("Brave search failed: {}", response.error().message) });

    if (response->statusCode != 200)
        return std::unexpected(
            ToolError { .message = std::format("Brave returned HTTP {}", response->statusCode) });

    auto results = std::vector<SearchResult> {};
    try
    {
        auto const json = nlohmann::json::parse(response->body);
        if (json.contains("web") && json["web"].contains("results"))
        {
            for (auto const& item: json["web"]["results"])
            {
                if (results.size() >= maxResults)
                    break;
                results.push_back(SearchResult {
                    .title = item.value("title", ""),
                    .url = item.value("url", ""),
                    .description = item.value("description", ""),
                });
            }
        }
    }
    catch (nlohmann::json::exception const& e)
    {
        return std::unexpected(
            ToolError { .message = std::format("Failed to parse Brave response: {}", e.what()) });
    }

    return results;
}

auto WebSearchTool::searchGoogle(std::string const& query, size_t maxResults)
    -> std::expected<std::vector<SearchResult>, ToolError>
{
    if (_config.apiKey.empty())
        return std::unexpected(ToolError { .message = "Google search requires an API key. "
                                                      "Set it with: set_web_search_api_key \"YOUR_KEY\"" });

    if (_config.cx.empty())
        return std::unexpected(ToolError { .message = "Google search requires a Custom Search Engine ID. "
                                                      "Set it with: set_web_search_cx \"YOUR_CX_ID\"" });

    auto const url = std::format("{}?q={}&key={}&cx={}&num={}",
                                 GoogleSearchUrl,
                                 urlEncode(query),
                                 _config.apiKey,
                                 _config.cx,
                                 maxResults);

    auto request = http::HttpRequest {};
    request.url = url;
    request.method = http::HttpMethod::Get;
    request.headers = { "Accept: application/json" };
    request.timeout = std::chrono::seconds { 15 };

    auto response = _httpClient.execute(request);
    if (!response.has_value())
        return std::unexpected(
            ToolError { .message = std::format("Google search failed: {}", response.error().message) });

    if (response->statusCode != 200)
        return std::unexpected(
            ToolError { .message = std::format("Google returned HTTP {}", response->statusCode) });

    auto results = std::vector<SearchResult> {};
    try
    {
        auto const json = nlohmann::json::parse(response->body);
        if (json.contains("items"))
        {
            for (auto const& item: json["items"])
            {
                if (results.size() >= maxResults)
                    break;
                results.push_back(SearchResult {
                    .title = item.value("title", ""),
                    .url = item.value("link", ""),
                    .description = item.value("snippet", ""),
                });
            }
        }
    }
    catch (nlohmann::json::exception const& e)
    {
        return std::unexpected(
            ToolError { .message = std::format("Failed to parse Google response: {}", e.what()) });
    }

    return results;
}

auto WebSearchTool::formatResults(std::string const& query, std::vector<SearchResult> const& results)
    -> std::string
{
    auto output = std::format("## Search Results for \"{}\"\n\n", query);

    for (size_t i = 0; i < results.size(); ++i)
    {
        auto const& r = results[i];
        output += std::format("{}. **{}**\n", i + 1, r.title);
        output += std::format("   URL: {}\n", r.url);
        if (!r.description.empty())
            output += std::format("   {}\n", r.description);
        output += '\n';
    }

    return output;
}

auto WebSearchTool::urlEncode(std::string_view input) -> std::string
{
    auto encoded = std::string {};
    encoded.reserve(input.size() * 3);

    for (auto const ch: input)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            encoded += ch;
        else
            encoded += std::format("%{:02X}", static_cast<unsigned char>(ch));
    }

    return encoded;
}

auto WebSearchTool::stripHtmlTags(std::string_view html) -> std::string
{
    auto result = std::string {};
    result.reserve(html.size());

    auto inTag = false;
    for (auto const ch: html)
    {
        if (ch == '<')
            inTag = true;
        else if (ch == '>')
            inTag = false;
        else if (!inTag)
            result += ch;
    }

    return result;
}

auto WebSearchTool::decodeHtmlEntities(std::string_view text) -> std::string
{
    auto result = std::string(text);

    // Common HTML entities
    auto replaceAll = [](std::string& str, std::string_view from, std::string_view to) {
        auto pos = size_t { 0 };
        while ((pos = str.find(from, pos)) != std::string::npos)
        {
            str.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replaceAll(result, "&amp;", "&");
    replaceAll(result, "&lt;", "<");
    replaceAll(result, "&gt;", ">");
    replaceAll(result, "&quot;", "\"");
    replaceAll(result, "&#39;", "'");
    replaceAll(result, "&apos;", "'");
    replaceAll(result, "&#x27;", "'");
    replaceAll(result, "&nbsp;", " ");

    return result;
}

} // namespace endo::agent
