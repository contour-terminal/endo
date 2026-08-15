// SPDX-License-Identifier: Apache-2.0
#include <agent/tools/HtmlUtils.hpp>
#include <platform/FileUri.hpp>

namespace endo::agent
{

auto urlEncode(std::string_view input) -> std::string
{
    // Delegated rather than reimplemented: this used to classify bytes with std::isalnum, which is
    // locale-dependent and leaves high bytes unencoded under some locales.
    return platform::percentEncode(input);
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
