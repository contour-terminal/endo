// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#include <net/HttpServer.hpp>

namespace endo::net
{

namespace
{
    /// Case-insensitively compares two ASCII strings for equality.
    [[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept
    {
        return std::ranges::equal(
            a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    /// Trims leading/trailing ASCII whitespace from a view.
    [[nodiscard]] std::string_view trim(std::string_view s) noexcept
    {
        constexpr auto Whitespace = std::string_view { " \t\r\n\f\v" };
        auto const first = s.find_first_not_of(Whitespace);
        if (first == std::string_view::npos)
            return {};
        auto const last = s.find_last_not_of(Whitespace);
        return s.substr(first, last - first + 1);
    }

    /// Parses the request line + headers from @p headerText into @p request.
    /// @return The advertised Content-Length, or 0 if none/invalid.
    [[nodiscard]] std::size_t parseHead(std::string_view headerText, HttpRequest& request)
    {
        std::size_t contentLength = 0;
        auto lineStart = std::size_t { 0 };
        bool firstLine = true;
        while (lineStart < headerText.size())
        {
            auto lineEnd = headerText.find("\r\n", lineStart);
            if (lineEnd == std::string_view::npos)
                lineEnd = headerText.size();
            auto const line = headerText.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 2;

            if (firstLine)
            {
                firstLine = false;
                auto const sp1 = line.find(' ');
                auto const sp2 = (sp1 == std::string_view::npos) ? sp1 : line.find(' ', sp1 + 1);
                if (sp1 != std::string_view::npos && sp2 != std::string_view::npos)
                {
                    request.method = std::string { line.substr(0, sp1) };
                    request.path = std::string { line.substr(sp1 + 1, sp2 - sp1 - 1) };
                    request.version = std::string { line.substr(sp2 + 1) };
                }
                continue;
            }

            if (line.empty())
                continue;
            auto const colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            auto const name = trim(line.substr(0, colon));
            auto const value = trim(line.substr(colon + 1));
            request.headers.emplace_back(std::string { name }, std::string { value });
            if (iequals(name, "Content-Length"))
            {
                std::size_t parsed = 0;
                auto const [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (ec == std::errc {})
                    contentLength = parsed;
            }
        }
        return contentLength;
    }
} // namespace

std::string HttpRequest::header(std::string_view name) const
{
    for (auto const& [key, value]: headers)
        if (iequals(key, name))
            return value;
    return {};
}

HttpResponse HttpResponse::ok(std::string text)
{
    return HttpResponse { .status = 200, .reason = "OK", .headers = {}, .body = std::move(text) };
}

HttpResponse HttpResponse::withStatus(int status, std::string text)
{
    return HttpResponse {
        .status = status, .reason = (status == 200) ? "OK" : "", .headers = {}, .body = std::move(text)
    };
}

endo::coro::Task<std::expected<HttpRequest, NetError>> readRequest(ISocket* socket)
{
    std::string buffer;
    auto chunk = std::array<std::byte, 4096> {};

    // Read until the end of the header block (CRLFCRLF).
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos)
    {
        auto const got = co_await socket->read(chunk);
        if (!got.has_value())
            co_return std::unexpected(got.error());
        if (*got == 0)
            co_return std::unexpected(makeNetError(NetErrorCode::Eof, 0, "eof before request headers"));
        buffer.append(reinterpret_cast<char const*>(chunk.data()), *got);
        headerEnd = buffer.find("\r\n\r\n");
        if (buffer.size() > (64U * 1024U) && headerEnd == std::string::npos)
            co_return std::unexpected(makeNetError(NetErrorCode::Other, 0, "request headers too large"));
    }

    auto request = HttpRequest {};
    auto const contentLength = parseHead(std::string_view { buffer }.substr(0, headerEnd), request);

    // The header block is followed by the body; we may already have some of it.
    auto bodyStart = headerEnd + 4;
    auto body = buffer.substr(bodyStart);
    while (body.size() < contentLength)
    {
        auto const got = co_await socket->read(chunk);
        if (!got.has_value())
            co_return std::unexpected(got.error());
        if (*got == 0)
            break; // peer closed mid-body; deliver what we have
        body.append(reinterpret_cast<char const*>(chunk.data()), *got);
    }
    request.body = std::move(body);
    co_return request;
}

endo::coro::Task<IoResult> writeResponse(ISocket* socket, HttpResponse response)
{
    auto out = std::string {};
    out += "HTTP/1.1 ";
    out += std::to_string(response.status);
    out += ' ';
    out += response.reason;
    out += "\r\n";

    bool hasContentType = false;
    for (auto const& [name, value]: response.headers)
    {
        out += name;
        out += ": ";
        out += value;
        out += "\r\n";
        if (name == "Content-Type")
            hasContentType = true;
    }
    if (!hasContentType)
        out += "Content-Type: text/plain; charset=utf-8\r\n";
    out += "Content-Length: ";
    out += std::to_string(response.body.size());
    out += "\r\n";
    out += "Connection: close\r\n\r\n";
    out += response.body;

    auto const bytes =
        std::span<std::byte const> { reinterpret_cast<std::byte const*>(out.data()), out.size() };
    co_return co_await socket->write(bytes);
}

namespace
{
    /// Handles one accepted connection: read a request, dispatch, write the response.
    endo::coro::Task<void> handleConnection(ISocket* socket, HttpHandler const* handler)
    {
        auto request = co_await readRequest(socket);
        if (!request.has_value())
            co_return; // malformed / EOF: drop the connection

        auto response = (*handler)(*request);
        static_cast<void>(co_await writeResponse(socket, std::move(response)));
    }
} // namespace

endo::coro::Task<void> serve(IListener* listener, HttpHandler handler)
{
    while (true)
    {
        auto accepted = co_await listener->accept();
        if (!accepted.has_value())
            co_return; // listener closed / cancelled
        auto conn = std::move(*accepted);
        co_await handleConnection(conn.get(), &handler);
    }
}

} // namespace endo::net
