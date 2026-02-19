// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/HtmlUtils.hpp>

using namespace endo::agent;

TEST_CASE("HtmlUtils.urlEncode_alphanumeric_passthrough", "[agent][tools]")
{
    CHECK(urlEncode("hello123") == "hello123");
}

TEST_CASE("HtmlUtils.urlEncode_special_chars", "[agent][tools]")
{
    CHECK(urlEncode("hello world") == "hello%20world");
    CHECK(urlEncode("a+b=c") == "a%2Bb%3Dc");
}

TEST_CASE("HtmlUtils.urlEncode_unreserved_chars_passthrough", "[agent][tools]")
{
    CHECK(urlEncode("a-b_c.d~e") == "a-b_c.d~e");
}

TEST_CASE("HtmlUtils.urlEncode_empty_string", "[agent][tools]")
{
    CHECK(urlEncode("") == "");
}

TEST_CASE("HtmlUtils.stripHtmlTags_basic", "[agent][tools]")
{
    CHECK(stripHtmlTags("<b>bold</b>") == "bold");
}

TEST_CASE("HtmlUtils.stripHtmlTags_nested", "[agent][tools]")
{
    CHECK(stripHtmlTags("<div><p>text</p></div>") == "text");
}

TEST_CASE("HtmlUtils.stripHtmlTags_no_tags", "[agent][tools]")
{
    CHECK(stripHtmlTags("plain text") == "plain text");
}

TEST_CASE("HtmlUtils.stripHtmlTags_empty", "[agent][tools]")
{
    CHECK(stripHtmlTags("") == "");
}

TEST_CASE("HtmlUtils.decodeHtmlEntities_common_entities", "[agent][tools]")
{
    CHECK(decodeHtmlEntities("&amp;") == "&");
    CHECK(decodeHtmlEntities("&lt;") == "<");
    CHECK(decodeHtmlEntities("&gt;") == ">");
    CHECK(decodeHtmlEntities("&quot;") == "\"");
    CHECK(decodeHtmlEntities("&#39;") == "'");
    CHECK(decodeHtmlEntities("&apos;") == "'");
    CHECK(decodeHtmlEntities("&#x27;") == "'");
    CHECK(decodeHtmlEntities("&nbsp;") == " ");
}

TEST_CASE("HtmlUtils.decodeHtmlEntities_mixed_text", "[agent][tools]")
{
    CHECK(decodeHtmlEntities("A &amp; B &lt; C") == "A & B < C");
}

TEST_CASE("HtmlUtils.decodeHtmlEntities_no_entities", "[agent][tools]")
{
    CHECK(decodeHtmlEntities("plain text") == "plain text");
}

TEST_CASE("HtmlUtils.decodeHtmlEntities_empty", "[agent][tools]")
{
    CHECK(decodeHtmlEntities("") == "");
}
