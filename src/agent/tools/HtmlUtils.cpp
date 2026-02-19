// SPDX-License-Identifier: Apache-2.0
#include <cctype>
#include <format>

#include <agent/tools/HtmlUtils.hpp>

namespace endo::agent
{

auto urlEncode(std::string_view input) -> std::string
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

auto stripHtmlTags(std::string_view html) -> std::string
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

auto decodeHtmlEntities(std::string_view text) -> std::string
{
    auto result = std::string(text);

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
