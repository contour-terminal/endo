// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <catch2/catch_test_macros.hpp>

#include <agent/tools/WebFetchTool.hpp>

using namespace endo::agent;

namespace
{

auto makeDefaultConfig() -> WebFetchConfig
{
    return WebFetchConfig {};
}

} // namespace

TEST_CASE("WebFetchTool.name", "[agent][tools]")
{
    auto const httpClient = endo::http::HttpClient {};
    auto const config = makeDefaultConfig();
    auto const tool = WebFetchTool(httpClient, config);
    CHECK(tool.name() == "web_fetch");
}

TEST_CASE("WebFetchTool.definition_schema", "[agent][tools]")
{
    auto const httpClient = endo::http::HttpClient {};
    auto const config = makeDefaultConfig();
    auto const tool = WebFetchTool(httpClient, config);

    auto const def = tool.definition();
    CHECK(def.name == "web_fetch");
    CHECK_FALSE(def.description.empty());
    CHECK(def.inputSchema.contains("properties"));
    CHECK(def.inputSchema["properties"].contains("url"));
    CHECK(def.inputSchema["properties"].contains("max_length"));

    auto const& required = def.inputSchema["required"];
    REQUIRE(required.is_array());
    CHECK(required.size() == 1);
    CHECK(required[0] == "url");
}

TEST_CASE("WebFetchTool.missing_url_error", "[agent][tools]")
{
    auto const httpClient = endo::http::HttpClient {};
    auto const config = makeDefaultConfig();
    auto tool = WebFetchTool(httpClient, config);

    auto const result = tool.execute(nlohmann::json::object());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Missing required parameter") != std::string::npos);
}

TEST_CASE("WebFetchTool.empty_url_error", "[agent][tools]")
{
    auto const httpClient = endo::http::HttpClient {};
    auto const config = makeDefaultConfig();
    auto tool = WebFetchTool(httpClient, config);

    auto const result = tool.execute(nlohmann::json { { "url", "" } });
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Missing required parameter") != std::string::npos);
}

TEST_CASE("WebFetchTool.invalid_url_scheme_error", "[agent][tools]")
{
    auto const httpClient = endo::http::HttpClient {};
    auto const config = makeDefaultConfig();
    auto tool = WebFetchTool(httpClient, config);

    auto const result = tool.execute(nlohmann::json { { "url", "ftp://example.com" } });
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("must start with http") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_headings", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<h1>Title</h1><h2>Subtitle</h2>");
    CHECK(md.find("# Title") != std::string::npos);
    CHECK(md.find("## Subtitle") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_links", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown(R"(<a href="https://example.com">Click here</a>)");
    CHECK(md.find("[Click here](https://example.com)") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_script_stripping", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<p>Hello</p><script>alert('bad')</script><p>World</p>");
    CHECK(md.find("Hello") != std::string::npos);
    CHECK(md.find("World") != std::string::npos);
    CHECK(md.find("alert") == std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_paragraphs", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<p>First paragraph</p><p>Second paragraph</p>");
    CHECK(md.find("First paragraph") != std::string::npos);
    CHECK(md.find("Second paragraph") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_code_blocks", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<pre>int x = 42;</pre>");
    CHECK(md.find("```") != std::string::npos);
    CHECK(md.find("int x = 42;") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_lists", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<ul><li>Item 1</li><li>Item 2</li></ul>");
    CHECK(md.find("- Item 1") != std::string::npos);
    CHECK(md.find("- Item 2") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_bold_italic", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<strong>bold</strong> and <em>italic</em>");
    CHECK(md.find("**bold**") != std::string::npos);
    CHECK(md.find("*italic*") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_entity_decoding", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<p>A &amp; B &lt; C</p>");
    CHECK(md.find("A & B < C") != std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_style_stripping", "[agent][tools]")
{
    auto const md = WebFetchTool::htmlToMarkdown("<style>body{color:red}</style><p>Text</p>");
    CHECK(md.find("Text") != std::string::npos);
    CHECK(md.find("color:red") == std::string::npos);
}

TEST_CASE("WebFetchTool.htmlToMarkdown_nav_footer_stripping", "[agent][tools]")
{
    auto const md =
        WebFetchTool::htmlToMarkdown("<nav>Navigation</nav><p>Content</p><footer>Footer</footer>");
    CHECK(md.find("Content") != std::string::npos);
    CHECK(md.find("Navigation") == std::string::npos);
    CHECK(md.find("Footer") == std::string::npos);
}
