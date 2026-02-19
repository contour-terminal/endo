// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include <agent/tools/HtmlUtils.hpp>
#include <agent/tools/WebFetchTool.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

WebFetchTool::WebFetchTool(http::HttpClient const& httpClient, WebFetchConfig const& config):
    _httpClient(httpClient), _config(config)
{
}

auto WebFetchTool::name() const noexcept -> std::string_view
{
    return "web_fetch";
}

auto WebFetchTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "web_fetch",
        .description = "Fetches the content of a web page and returns it as readable text. "
                       "HTML is converted to markdown, JSON is pretty-printed. "
                       "Use this after web_search to read a specific page.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  nlohmann::json {
                      { "url",
                        nlohmann::json {
                            { "type", "string" },
                            { "description", "URL to fetch (http:// auto-upgraded to https://)." },
                        } },
                      { "max_length",
                        nlohmann::json {
                            { "type", "integer" },
                            { "description", "Max characters in returned content (default: 30000)." },
                        } },
                  } },
                { "required", nlohmann::json::array({ "url" }) },
            },
    };
}

auto WebFetchTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto url = arguments.value("url", std::string {});
    if (url.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: url" });

    // Validate URL scheme
    if (!url.starts_with("http://") && !url.starts_with("https://"))
        return std::unexpected(ToolError { .message = "Invalid URL: must start with http:// or https://" });

    // Auto-upgrade http to https
    if (url.starts_with("http://"))
        url = "https://" + url.substr(7);

    auto const maxLength = arguments.value("max_length", static_cast<int>(_config.maxOutputLength));

    // Check cache (lazy eviction)
    if (auto it = _cache.find(url); it != _cache.end())
    {
        if (std::chrono::steady_clock::now() < it->second.expiry)
        {
            auto content = it->second.content;
            if (content.size() > static_cast<size_t>(maxLength))
            {
                auto const omitted = content.size() - static_cast<size_t>(maxLength);
                content.resize(static_cast<size_t>(maxLength));
                content += std::format("\n\n[truncated — {} characters omitted]", omitted);
            }
            return ToolResult { .content = std::move(content), .isError = false };
        }
        _cache.erase(it);
    }

    // Perform HTTP request
    auto request = http::HttpRequest {};
    request.url = url;
    request.method = http::HttpMethod::Get;
    request.headers = { "User-Agent: Mozilla/5.0 (compatible; EndoShell/1.0)" };
    request.timeout = _config.requestTimeout;
    request.maxResponseSize = _config.maxContentSize;
    request.followRedirects = true;

    auto response = _httpClient.execute(request);
    if (!response.has_value())
        return std::unexpected(
            ToolError { .message = std::format("Request failed: {}", response.error().message) });

    if (response->statusCode < 200 || response->statusCode >= 300)
        return std::unexpected(
            ToolError { .message = std::format("HTTP {} returned for {}", response->statusCode, url) });

    // Process content based on content type
    auto const contentType = extractContentType(response->headers);
    auto content = std::string {};

    if (contentType.find("text/html") != std::string::npos)
    {
        content = htmlToMarkdown(response->body);
    }
    else if (contentType.find("application/json") != std::string::npos)
    {
        try
        {
            auto const json = nlohmann::json::parse(response->body);
            content = json.dump(2);
        }
        catch (nlohmann::json::exception const&)
        {
            content = response->body; // Return raw if JSON parsing fails
        }
    }
    else
    {
        content = response->body;
    }

    // Cache the result
    _cache[url] = CacheEntry {
        .content = content,
        .expiry = std::chrono::steady_clock::now() + _config.cacheTtl,
    };

    // Truncate if needed
    if (content.size() > static_cast<size_t>(maxLength))
    {
        auto const omitted = content.size() - static_cast<size_t>(maxLength);
        content.resize(static_cast<size_t>(maxLength));
        content += std::format("\n\n[truncated — {} characters omitted]", omitted);
    }

    return ToolResult { .content = std::move(content), .isError = false };
}

auto WebFetchTool::htmlToMarkdown(std::string_view html) -> std::string
{
    auto result = std::string {};
    result.reserve(html.size());

    auto pos = size_t { 0 };
    auto inPre = false;
    auto inScript = false;
    auto inStyle = false;
    auto inNav = false;
    auto inFooter = false;

    while (pos < html.size())
    {
        if (html[pos] == '<')
        {
            // Find the end of this tag
            auto const tagEnd = html.find('>', pos);
            if (tagEnd == std::string::npos)
                break;

            auto const tagContent = html.substr(pos + 1, tagEnd - pos - 1);
            auto tagName = std::string {};
            auto isClosing = false;

            // Parse tag name
            auto tagStart = size_t { 0 };
            if (!tagContent.empty() && tagContent[0] == '/')
            {
                isClosing = true;
                tagStart = 1;
            }

            // Extract tag name (up to space or end)
            auto nameEnd = tagContent.find_first_of(" \t\n\r/", tagStart);
            if (nameEnd == std::string_view::npos)
                nameEnd = tagContent.size();
            tagName = std::string(tagContent.substr(tagStart, nameEnd - tagStart));

            // Convert to lowercase for comparison
            std::ranges::transform(
                tagName, tagName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            // Handle block-level exclusions
            if (tagName == "script")
                inScript = !isClosing;
            else if (tagName == "style")
                inStyle = !isClosing;
            else if (tagName == "nav")
                inNav = !isClosing;
            else if (tagName == "footer")
                inFooter = !isClosing;

            if (!inScript && !inStyle && !inNav && !inFooter)
            {
                if (tagName == "pre" || tagName == "code")
                {
                    if (!isClosing)
                    {
                        if (tagName == "pre")
                        {
                            inPre = true;
                            result += "\n```\n";
                        }
                    }
                    else
                    {
                        if (tagName == "pre")
                        {
                            inPre = false;
                            result += "\n```\n";
                        }
                    }
                }
                else if (!inPre)
                {
                    // Headings
                    if (!isClosing && tagName.size() == 2 && tagName[0] == 'h' && tagName[1] >= '1'
                        && tagName[1] <= '6')
                    {
                        auto const level = tagName[1] - '0';
                        result += '\n';
                        for (int i = 0; i < level; ++i)
                            result += '#';
                        result += ' ';
                    }
                    else if (isClosing && tagName.size() == 2 && tagName[0] == 'h' && tagName[1] >= '1'
                             && tagName[1] <= '6')
                    {
                        result += '\n';
                    }
                    // Paragraphs and line breaks
                    else if (tagName == "p" || tagName == "br" || tagName == "div")
                    {
                        result += "\n\n";
                    }
                    // Lists
                    else if (tagName == "li" && !isClosing)
                    {
                        result += "\n- ";
                    }
                    // Bold
                    else if ((tagName == "strong" || tagName == "b"))
                    {
                        result += "**";
                    }
                    // Italic
                    else if ((tagName == "em" || tagName == "i"))
                    {
                        result += "*";
                    }
                    // Links: <a href="url">
                    else if (tagName == "a" && !isClosing)
                    {
                        // Extract href
                        auto const hrefPos = tagContent.find("href=\"");
                        if (hrefPos != std::string_view::npos)
                        {
                            auto const urlStart = hrefPos + 6;
                            auto const urlEnd = tagContent.find('"', urlStart);
                            if (urlEnd != std::string_view::npos)
                            {
                                auto const linkUrl = tagContent.substr(urlStart, urlEnd - urlStart);
                                result += '[';
                                // We'll add the text content, then close with ](url) at </a>
                                // Store url for later - simple approach: find </a> and extract text
                                auto const closeTag = html.find("</a>", tagEnd);
                                if (closeTag != std::string_view::npos)
                                {
                                    auto const linkText = html.substr(tagEnd + 1, closeTag - tagEnd - 1);
                                    result += stripHtmlTags(linkText);
                                    result += "](";
                                    result += linkUrl;
                                    result += ')';
                                    pos = closeTag + 4;
                                    continue;
                                }
                            }
                        }
                    }
                }
            }

            pos = tagEnd + 1;
        }
        else if (inScript || inStyle || inNav || inFooter)
        {
            // Skip content inside excluded blocks
            ++pos;
        }
        else
        {
            result += html[pos];
            ++pos;
        }
    }

    // Decode HTML entities
    result = decodeHtmlEntities(result);

    // Collapse multiple blank lines to max two
    auto collapsed = std::string {};
    collapsed.reserve(result.size());
    auto consecutiveNewlines = 0;
    for (auto const ch: result)
    {
        if (ch == '\n')
        {
            ++consecutiveNewlines;
            if (consecutiveNewlines <= 2)
                collapsed += ch;
        }
        else
        {
            consecutiveNewlines = 0;
            collapsed += ch;
        }
    }

    return collapsed;
}

auto WebFetchTool::extractContentType(std::vector<std::string> const& headers) -> std::string
{
    for (auto const& header: headers)
    {
        // Headers are typically "Content-Type: text/html; charset=utf-8"
        if (auto const colonPos = header.find(':'); colonPos != std::string::npos)
        {
            auto name = header.substr(0, colonPos);
            std::ranges::transform(
                name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == "content-type")
            {
                auto const valueStart = header.find_first_not_of(" \t", colonPos + 1);
                if (valueStart != std::string::npos)
                    return header.substr(valueStart);
            }
        }
    }
    return {};
}

} // namespace endo::agent
