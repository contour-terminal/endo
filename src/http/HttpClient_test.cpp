// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "HttpClient.hpp"

using namespace endo::http;

// =============================================================================
// RAII and construction tests
// =============================================================================

TEST_CASE("http.client.construct_destruct")
{
    // Verify that constructing and destructing an HttpClient does not crash.
    HttpClient client;
}

TEST_CASE("http.client.move_construct")
{
    HttpClient a;
    HttpClient b(std::move(a));
    // b should be usable, a should be in a moved-from state
}

TEST_CASE("http.client.move_assign")
{
    HttpClient a;
    HttpClient b;
    b = std::move(a);
    // b should be usable
}

// =============================================================================
// Error handling tests
// =============================================================================

TEST_CASE("http.client.invalid_url")
{
    HttpClient client;
    auto result = client.get("not-a-valid-url");
    REQUIRE(!result.has_value());
    CHECK(result.error().curlCode != 0);
    CHECK(!result.error().message.empty());
}

TEST_CASE("http.client.connection_refused")
{
    HttpClient client;
    // Port 1 is unlikely to have a listener
    auto result = client.get("http://localhost:1");
    REQUIRE(!result.has_value());
    CHECK(result.error().curlCode != 0);
}

TEST_CASE("http.client.dns_failure")
{
    HttpClient client;
    auto result = client.get("http://this-domain-does-not-exist-at-all.invalid");
    REQUIRE(!result.has_value());
    CHECK(result.error().curlCode != 0);
}

TEST_CASE("http.client.max_response_size")
{
    HttpClient client;
    HttpRequest request {
        .url = "http://localhost:1", // Won't connect, but tests the setup path
        .maxResponseSize = 1,        // 1 byte limit
    };
    // This will fail with connection error, not max-size error, but verifies the field is accepted
    auto result = client.execute(request);
    REQUIRE(!result.has_value());
}

// =============================================================================
// Integration tests (requires network, tagged for optional execution)
// =============================================================================

TEST_CASE("http.client.fetch_public_url", "[.integration]")
{
    HttpClient client;
    auto result = client.get("https://httpbin.org/get");
    REQUIRE(result.has_value());
    CHECK(result->statusCode == 200);
    CHECK(!result->body.empty());
    CHECK(!result->headers.empty());
}

TEST_CASE("http.client.fetch_with_headers", "[.integration]")
{
    HttpClient client;
    auto result = client.get("https://httpbin.org/headers", { "X-Custom-Test: hello" });
    REQUIRE(result.has_value());
    CHECK(result->statusCode == 200);
    CHECK(result->body.find("X-Custom-Test") != std::string::npos);
}

TEST_CASE("http.client.fetch_404", "[.integration]")
{
    HttpClient client;
    auto result = client.get("https://httpbin.org/status/404");
    REQUIRE(result.has_value());
    CHECK(result->statusCode == 404);
}

TEST_CASE("http.client.timeout", "[.integration]")
{
    HttpClient client;
    HttpRequest request {
        .url = "https://httpbin.org/delay/10",
        .timeout = std::chrono::seconds(2),
    };
    auto result = client.execute(request);
    REQUIRE(!result.has_value());
    // Should timeout
    CHECK(result.error().curlCode != 0);
}

// =============================================================================
// extractFilenameFromUrl tests
// =============================================================================

TEST_CASE("http.extractFilenameFromUrl.with_extension")
{
    auto result = extractFilenameFromUrl("https://example.com/path/file.tar.gz");
    REQUIRE(result.has_value());
    CHECK(*result == "file.tar.gz");
}

TEST_CASE("http.extractFilenameFromUrl.strip_query_and_fragment")
{
    auto result = extractFilenameFromUrl("https://example.com/path/data?q=1#frag");
    REQUIRE(result.has_value());
    CHECK(*result == "data");
}

TEST_CASE("http.extractFilenameFromUrl.root_path")
{
    auto result = extractFilenameFromUrl("https://example.com/");
    CHECK(!result.has_value());
}

TEST_CASE("http.extractFilenameFromUrl.no_path")
{
    auto result = extractFilenameFromUrl("https://example.com");
    CHECK(!result.has_value());
}

TEST_CASE("http.extractFilenameFromUrl.trailing_slash")
{
    auto result = extractFilenameFromUrl("https://example.com/path/");
    CHECK(!result.has_value());
}

TEST_CASE("http.extractFilenameFromUrl.deep_path")
{
    auto result = extractFilenameFromUrl("https://cdn.example.com/a/b/c/archive.zip");
    REQUIRE(result.has_value());
    CHECK(*result == "archive.zip");
}

TEST_CASE("http.extractFilenameFromUrl.query_only")
{
    auto result = extractFilenameFromUrl("https://example.com/download?file=test");
    REQUIRE(result.has_value());
    CHECK(*result == "download");
}

// =============================================================================
// download() error tests
// =============================================================================

TEST_CASE("http.client.download.invalid_url")
{
    HttpClient client;
    auto const tempDir = std::filesystem::temp_directory_path();
    auto const outputPath = tempDir / "test_download_invalid";
    HttpRequest request { .url = "not-a-valid-url" };

    auto result = client.download(request, outputPath);
    REQUIRE(!result.has_value());
    CHECK(result.error().curlCode != 0);
    // Partial file should be cleaned up
    CHECK(!std::filesystem::exists(outputPath));
}

TEST_CASE("http.client.download.connection_refused")
{
    HttpClient client;
    auto const tempDir = std::filesystem::temp_directory_path();
    auto const outputPath = tempDir / "test_download_connrefused";
    HttpRequest request { .url = "http://localhost:1" };

    auto result = client.download(request, outputPath);
    REQUIRE(!result.has_value());
    CHECK(result.error().curlCode != 0);
    // Partial file should be cleaned up
    CHECK(!std::filesystem::exists(outputPath));
}

TEST_CASE("http.client.download.public_url", "[.integration]")
{
    HttpClient client;
    auto const tempDir = std::filesystem::temp_directory_path();
    auto const outputPath = tempDir / "test_download_httpbin";
    HttpRequest request { .url = "https://httpbin.org/get" };

    auto result = client.download(request, outputPath);
    REQUIRE(result.has_value());
    CHECK(result->statusCode == 200);
    CHECK(result->body.empty()); // Body streamed to file, not stored in response
    CHECK(!result->headers.empty());
    CHECK(std::filesystem::exists(outputPath));
    CHECK(std::filesystem::file_size(outputPath) > 0);

    // Cleanup
    std::filesystem::remove(outputPath);
}
