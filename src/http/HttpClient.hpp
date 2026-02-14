// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace endo::http
{

/// Error information from a failed HTTP request.
struct HttpError
{
    int curlCode;        ///< libcurl error code
    std::string message; ///< Human-readable error description
};

/// HTTP request methods.
enum class HttpMethod : uint8_t
{
    Get,
    Post,
    Put,
    Delete,
    Head,
    Patch,
};

/// Callback for reporting download progress.
/// @param total Total expected bytes (0 if unknown)
/// @param now   Bytes transferred so far
/// @return false to abort the transfer, true to continue
using ProgressCallback = std::function<bool(size_t total, size_t now)>;

/// Configuration for an HTTP request.
struct HttpRequest
{
    std::string url;
    HttpMethod method = HttpMethod::Get;
    std::vector<std::string> headers;            ///< Headers in "Key: Value" format
    std::string body;                            ///< Request body for POST/PUT/PATCH
    std::optional<std::chrono::seconds> timeout; ///< Request timeout
    size_t maxResponseSize = 64 * 1024 * 1024;   ///< Maximum response body size (64 MB)
    ProgressCallback progressCallback;           ///< Optional progress reporting callback
    bool followRedirects = true;                 ///< Whether to follow HTTP redirects
    long maxRedirects = 10;                      ///< Maximum number of redirects to follow
};

/// Response from a successful HTTP request.
struct HttpResponse
{
    long statusCode;                  ///< HTTP status code (e.g. 200, 404)
    std::string body;                 ///< Response body
    std::vector<std::string> headers; ///< Response headers
};

/// RAII wrapper around libcurl for performing HTTP requests.
///
/// Each HttpClient instance owns a CURL easy handle. The class manages
/// global curl initialization via reference counting.
class HttpClient
{
  public:
    HttpClient();
    ~HttpClient();

    HttpClient(HttpClient const&) = delete;
    HttpClient& operator=(HttpClient const&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    /// Executes an HTTP request with full configuration.
    /// @param request The request configuration
    /// @return The HTTP response on success, or an HttpError on failure
    [[nodiscard]] std::expected<HttpResponse, HttpError> execute(HttpRequest const& request) const;

    /// Convenience method for a simple GET request.
    /// @param url     The URL to fetch
    /// @param headers Optional request headers in "Key: Value" format
    /// @return The HTTP response on success, or an HttpError on failure
    [[nodiscard]] std::expected<HttpResponse, HttpError> get(std::string url,
                                                             std::vector<std::string> headers = {}) const;

    /// Downloads the response body directly to a file, streaming without memory accumulation.
    /// @param request    The request configuration (maxResponseSize is ignored)
    /// @param outputPath Local file path to write the response body to
    /// @return HttpResponse with statusCode and headers populated (body is empty)
    [[nodiscard]] std::expected<HttpResponse, HttpError> download(
        HttpRequest const& request, std::filesystem::path const& outputPath) const;

  private:
    /// Sets up common curl options shared between execute() and download().
    void setupRequest(void* curl, HttpRequest const& request, void* headerList) const;

    void* _handle = nullptr; ///< CURL easy handle (void* to avoid leaking curl headers)
};

/// Extracts a filename from a URL for use as a local download target.
/// Takes the last non-empty path segment, strips query string and fragment.
/// @param url The URL to extract a filename from
/// @return The filename, or std::nullopt if no filename can be determined
[[nodiscard]] std::optional<std::string> extractFilenameFromUrl(std::string_view url);

} // namespace endo::http
