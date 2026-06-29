// SPDX-License-Identifier: Apache-2.0
#include <net/Sockets.hpp>

#if !defined(_WIN32)

    #include <cerrno>
    #include <string>

    #include <sys/socket.h>

    #include <fcntl.h>
    #include <netdb.h>
    #include <unistd.h>

    #include <net/posix/PosixListener.hpp>
    #include <net/posix/PosixSocket.hpp>

namespace endo::net
{

std::expected<std::unique_ptr<IListener>, NetError> listen(tui::runtime::TuiRuntime& runtime,
                                                           std::string_view host,
                                                           std::uint16_t port,
                                                           int backlog)
{
    return PosixListener::bind(runtime, host, port, backlog)
        .transform(
            [](std::unique_ptr<PosixListener> listener) -> std::unique_ptr<IListener> { return listener; });
}

endo::coro::Task<std::expected<std::unique_ptr<ISocket>, NetError>> connect(tui::runtime::TuiRuntime* runtime,
                                                                            std::string_view host,
                                                                            std::uint16_t port)
{
    auto hints = addrinfo {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    auto const hostStr = std::string { host };
    auto const portStr = std::to_string(port);

    addrinfo* resolved = nullptr;
    auto const rc = ::getaddrinfo(hostStr.c_str(), portStr.c_str(), &hints, &resolved);
    if (rc != 0 || resolved == nullptr)
        co_return std::unexpected(makeNetError(NetErrorCode::AddressError, rc, "getaddrinfo"));

    NetError lastError = makeNetError(NetErrorCode::AddressError, 0, "no usable address");
    for (auto const* ai = resolved; ai != nullptr; ai = ai->ai_next)
    {
        auto const fd =
            ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0)
        {
            lastError = makeNetError(NetErrorCode::Other, errno, "socket");
            continue;
        }

        auto const rcConnect = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rcConnect == 0)
        {
            ::freeaddrinfo(resolved);
            co_return std::unique_ptr<ISocket>(new PosixSocket(*runtime, fd));
        }
        if (errno == EINPROGRESS)
        {
            // Non-blocking connect in progress: park until writable, then check the
            // pending socket error to learn whether it succeeded.
            try
            {
                co_await runtime->waitWritable(fd);
            }
            catch (endo::coro::OperationCancelled const&)
            {
                ::close(fd);
                ::freeaddrinfo(resolved);
                co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "connect cancelled"));
            }

            int soError = 0;
            auto soLen = socklen_t { sizeof(soError) };
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soLen);
            if (soError == 0)
            {
                ::freeaddrinfo(resolved);
                co_return std::unique_ptr<ISocket>(new PosixSocket(*runtime, fd));
            }
            lastError =
                makeNetError(soError == ECONNREFUSED ? NetErrorCode::ConnRefused : NetErrorCode::Other,
                             soError,
                             "connect");
        }
        else
        {
            lastError = makeNetError(
                errno == ECONNREFUSED ? NetErrorCode::ConnRefused : NetErrorCode::Other, errno, "connect");
        }
        ::close(fd);
    }
    ::freeaddrinfo(resolved);
    co_return std::unexpected(lastError);
}

} // namespace endo::net

#endif // !_WIN32
