// SPDX-License-Identifier: Apache-2.0
#include "HttpClient.hpp"

#include <fstream>
#include <mutex>
#include <utility>

#include <curl/curl.h>

namespace endo::http
{

namespace
{

    /// Reference-counted global curl initialization.
    struct CurlGlobalGuard
    {
        static void acquire()
        {
            std::lock_guard lock { mutex };
            if (refCount++ == 0)
                curl_global_init(CURL_GLOBAL_DEFAULT);
        }

        static void release()
        {
            std::lock_guard lock { mutex };
            if (--refCount == 0)
                curl_global_cleanup();
        }

      private:
        static inline std::mutex mutex;
        static inline int refCount = 0;
    };

    /// Write callback for collecting the response body.
    size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* context = static_cast<std::pair<std::string*, size_t>*>(userdata);
        auto& [body, maxSize] = *context;
        auto const totalSize = size * nmemb;
        if (body->size() + totalSize > maxSize)
            return 0; // Abort: response exceeds maxResponseSize
        body->append(ptr, totalSize);
        return totalSize;
    }

    /// Header callback for collecting response headers.
    size_t headerCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* headers = static_cast<std::vector<std::string>*>(userdata);
        auto const totalSize = size * nmemb;
        std::string header(ptr, totalSize);
        // Strip trailing \r\n
        while (!header.empty() && (header.back() == '\r' || header.back() == '\n'))
            header.pop_back();
        if (!header.empty())
            headers->push_back(std::move(header));
        return totalSize;
    }

    /// Progress callback wrapper for CURLOPT_XFERINFOFUNCTION.
    int progressCallbackWrapper(void* clientp,
                                curl_off_t dltotal,
                                curl_off_t dlnow,
                                [[maybe_unused]] curl_off_t ultotal,
                                [[maybe_unused]] curl_off_t ulnow)
    {
        auto* callback = static_cast<ProgressCallback*>(clientp);
        if (*callback)
        {
            auto const cont = (*callback)(static_cast<size_t>(dltotal), static_cast<size_t>(dlnow));
            return cont ? 0 : 1; // 0 = continue, non-zero = abort
        }
        return 0;
    }

    /// Maps a CURLcode to an HttpError.
    HttpError makeError(CURLcode code)
    {
        return HttpError { .curlCode = static_cast<int>(code), .message = curl_easy_strerror(code) };
    }

    /// Maps a CURLE_WRITE_ERROR with max-size context to a friendlier message.
    HttpError makeMaxSizeError(size_t maxResponseSize)
    {
        return HttpError { .curlCode = static_cast<int>(CURLE_WRITE_ERROR),
                           .message = std::format("Response body exceeded maximum size of {} bytes",
                                                  maxResponseSize) };
    }

    /// Write callback that streams data directly to a file.
    size_t fileWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* file = static_cast<std::ofstream*>(userdata);
        auto const totalSize = size * nmemb;
        file->write(ptr, static_cast<std::streamsize>(totalSize));
        return file->good() ? totalSize : 0;
    }

    /// State for the incremental SSE parser used during streaming.
    struct SseParserState
    {
        std::string buffer;       ///< Accumulated raw data not yet parsed.
        std::string currentEvent; ///< Current "event:" value.
        std::string currentData;  ///< Accumulated "data:" lines for the current event.
        std::string currentId;    ///< Current "id:" value.
        SseCallback const* callback = nullptr;
        bool aborted = false;

        /// Processes a single line of SSE input.
        void processLine(std::string_view line)
        {
            if (aborted)
                return;

            // Empty line dispatches the event
            if (line.empty())
            {
                if (!currentData.empty())
                {
                    // Remove trailing newline from data if present
                    if (currentData.back() == '\n')
                        currentData.pop_back();

                    auto event = SseEvent {
                        .event = std::move(currentEvent),
                        .data = std::move(currentData),
                        .id = std::move(currentId),
                    };
                    currentEvent.clear();
                    currentData.clear();
                    currentId.clear();

                    if (callback && !(*callback)(event))
                        aborted = true;
                }
                return;
            }

            // Comment lines (starting with ':') are ignored
            if (line.starts_with(':'))
                return;

            // Parse "field: value" or "field:value"
            auto const colonPos = line.find(':');
            if (colonPos == std::string_view::npos)
            {
                // Field with no value — treat as field with empty value
                // (SSE spec says field names without ':' set the field to "")
                return;
            }

            auto const field = line.substr(0, colonPos);
            auto value = line.substr(colonPos + 1);
            // Strip single leading space after colon (SSE spec)
            if (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);

            if (field == "data")
            {
                currentData += value;
                currentData += '\n';
            }
            else if (field == "event")
            {
                currentEvent = std::string(value);
            }
            else if (field == "id")
            {
                currentId = std::string(value);
            }
            // Other fields are ignored per SSE spec
        }

        /// Feeds raw data from curl into the parser, extracting complete lines.
        void feed(std::string_view chunk)
        {
            buffer += chunk;

            // Process all complete lines in the buffer
            while (!aborted)
            {
                auto const nlPos = buffer.find('\n');
                if (nlPos == std::string::npos)
                    break;

                auto line = std::string_view(buffer).substr(0, nlPos);
                // Strip trailing \r for \r\n line endings
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);

                processLine(line);
                buffer.erase(0, nlPos + 1);
            }
        }
    };

    /// Write callback for SSE streaming that feeds data into the parser.
    size_t sseWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* state = static_cast<SseParserState*>(userdata);
        auto const totalSize = size * nmemb;
        state->feed(std::string_view(ptr, totalSize));
        return state->aborted ? 0 : totalSize;
    }

} // namespace

HttpClient::HttpClient()
{
    CurlGlobalGuard::acquire();
    _handle = curl_easy_init();
}

HttpClient::~HttpClient()
{
    if (_handle)
    {
        curl_easy_cleanup(static_cast<CURL*>(_handle));
        _handle = nullptr;
    }
    CurlGlobalGuard::release();
}

HttpClient::HttpClient(HttpClient&& other) noexcept: _handle(std::exchange(other._handle, nullptr))
{
    CurlGlobalGuard::acquire(); // Moved-from still holds a global ref
}

HttpClient& HttpClient::operator=(HttpClient&& other) noexcept
{
    if (this != &other)
    {
        if (_handle)
            curl_easy_cleanup(static_cast<CURL*>(_handle));
        _handle = std::exchange(other._handle, nullptr);
        // No need to adjust global ref count: both objects already hold one
    }
    return *this;
}

void HttpClient::setupRequest(void* curlHandle, HttpRequest const& request, void* slist) const
{
    auto* curl = static_cast<CURL*>(curlHandle);
    auto* headerList = static_cast<curl_slist*>(slist);

    // URL
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());

    // Method
    switch (request.method)
    {
        case HttpMethod::Get: break; // Default
        case HttpMethod::Post: curl_easy_setopt(curl, CURLOPT_POST, 1L); break;
        case HttpMethod::Put: curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); break;
        case HttpMethod::Delete: curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"); break;
        case HttpMethod::Head: curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); break;
        case HttpMethod::Patch: curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH"); break;
    }

    // Request body (POST/PUT/PATCH)
    if (!request.body.empty())
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }

    // Request headers
    if (headerList)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

    // Redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, request.maxRedirects);

    // Timeout
    if (request.timeout.has_value())
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(request.timeout->count()));
}

std::expected<HttpResponse, HttpError> HttpClient::execute(HttpRequest const& request) const
{
    if (!_handle)
        return std::unexpected(HttpError { .curlCode = -1, .message = "CURL handle not initialized" });

    auto* curl = static_cast<CURL*>(_handle);

    // Reset handle state from any prior request
    curl_easy_reset(curl);

    // Build request header list
    curl_slist* headerList = nullptr;
    for (auto const& h: request.headers)
        headerList = curl_slist_append(headerList, h.c_str());

    setupRequest(curl, request, headerList);

    // Write callback for response body
    HttpResponse response {};
    auto writeContext = std::make_pair(&response.body, request.maxResponseSize);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeContext);

    // Header callback for response headers
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

    // Progress callback
    auto progressCb = request.progressCallback; // Copy for stable address
    if (progressCb)
    {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallbackWrapper);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressCb);
    }

    // Perform the request
    auto const result = curl_easy_perform(curl);

    // Clean up request headers
    if (headerList)
        curl_slist_free_all(headerList);

    if (result != CURLE_OK)
    {
        if (result == CURLE_WRITE_ERROR && response.body.size() >= request.maxResponseSize)
            return std::unexpected(makeMaxSizeError(request.maxResponseSize));
        return std::unexpected(makeError(result));
    }

    // Get status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);

    return response;
}

std::expected<HttpResponse, HttpError> HttpClient::get(std::string url,
                                                       std::vector<std::string> headers) const
{
    return execute(HttpRequest {
        .url = std::move(url),
        .method = HttpMethod::Get,
        .headers = std::move(headers),
    });
}

std::expected<HttpResponse, HttpError> HttpClient::download(HttpRequest const& request,
                                                            std::filesystem::path const& outputPath) const
{
    if (!_handle)
        return std::unexpected(HttpError { .curlCode = -1, .message = "CURL handle not initialized" });

    auto* curl = static_cast<CURL*>(_handle);

    // Reset handle state from any prior request
    curl_easy_reset(curl);

    // Open output file
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open())
        return std::unexpected(HttpError {
            .curlCode = -1, .message = std::format("Failed to open output file: {}", outputPath.string()) });

    // Build request header list
    curl_slist* headerList = nullptr;
    for (auto const& h: request.headers)
        headerList = curl_slist_append(headerList, h.c_str());

    setupRequest(curl, request, headerList);

    // File write callback (streams directly to disk)
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);

    // Header callback for response headers
    HttpResponse response {};
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

    // Progress callback
    auto progressCb = request.progressCallback; // Copy for stable address
    if (progressCb)
    {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallbackWrapper);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressCb);
    }

    // Perform the request
    auto const result = curl_easy_perform(curl);

    // Clean up request headers
    if (headerList)
        curl_slist_free_all(headerList);

    // Close file before any error handling that may delete it
    file.close();

    if (result != CURLE_OK)
    {
        std::filesystem::remove(outputPath);
        return std::unexpected(makeError(result));
    }

    // Get status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);

    return response;
}

std::expected<long, HttpError> HttpClient::executeStreaming(HttpRequest const& request,
                                                            SseCallback const& callback,
                                                            std::string* errorBody) const
{
    if (!_handle)
        return std::unexpected(HttpError { .curlCode = -1, .message = "CURL handle not initialized" });

    auto* curl = static_cast<CURL*>(_handle);

    // Reset handle state from any prior request
    curl_easy_reset(curl);

    // Build request header list
    curl_slist* headerList = nullptr;
    for (auto const& h: request.headers)
        headerList = curl_slist_append(headerList, h.c_str());

    setupRequest(curl, request, headerList);

    // SSE streaming write callback
    auto parserState = SseParserState {};
    parserState.callback = &callback;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sseWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &parserState);

    // Perform the request
    auto const result = curl_easy_perform(curl);

    // Clean up request headers
    if (headerList)
        curl_slist_free_all(headerList);

    // If the callback aborted, curl returns CURLE_WRITE_ERROR — that's expected
    if (result != CURLE_OK && !(result == CURLE_WRITE_ERROR && parserState.aborted))
        return std::unexpected(makeError(result));

    // Get status code
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

    // On non-200 responses, the body is plain JSON (not SSE-framed),
    // so it accumulates in the parser buffer without being dispatched.
    if (statusCode != 200 && errorBody)
        *errorBody = std::move(parserState.buffer);

    return statusCode;
}

std::optional<std::string> extractFilenameFromUrl(std::string_view url)
{
    // Find the start of the path (after ://)
    auto const schemeEnd = url.find("://");
    auto pathStart = (schemeEnd != std::string_view::npos) ? url.find('/', schemeEnd + 3) : url.find('/');
    if (pathStart == std::string_view::npos)
        return std::nullopt;

    // Strip query string and fragment
    auto path = url.substr(pathStart);
    if (auto const pos = path.find_first_of("?#"); pos != std::string_view::npos)
        path = path.substr(0, pos);

    // Find the last non-empty segment
    auto const lastSlash = path.rfind('/');
    if (lastSlash == std::string_view::npos)
        return std::nullopt;

    auto filename = path.substr(lastSlash + 1);
    if (filename.empty())
        return std::nullopt;

    return std::string(filename);
}

} // namespace endo::http
