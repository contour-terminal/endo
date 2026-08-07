// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <CoreVM/CoreVM.hpp>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

#include <coro/Task.hpp>
#include <net/DefaultEventSource.hpp>
#include <net/EventLoop.hpp>
#include <net/HttpServer.hpp>
#include <net/Sockets.hpp>
#include <net/platform/SystemPipe.hpp>

namespace endo
{

namespace
{
    /// Invokes a script handler function (string -> string) with @p path and returns
    /// its result. Each request runs the handler in a fresh Runner over the shared
    /// program/globals via Runner::invoke, which seeds the argument and reads back
    /// the returned string value.
    /// @param shell The shell (for the program/globals and tracing).
    /// @param handler The user handler function.
    /// @param program The current program (provides the function's bytecode).
    /// @param globals The shared globals.
    /// @param path The request path passed as the handler's argument.
    /// @return The handler's returned body string (empty on failure).
    [[nodiscard]] std::string invokeHandler(CoreVM::Function const* handler,
                                            CoreVM::Runner::Globals* globals,
                                            std::string const& path)
    {
        auto runner =
            CoreVM::Runner(handler, nullptr, globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        auto const* arg = runner.newString(path);
        auto const args =
            std::array<CoreVM::Runner::Value, 1> { reinterpret_cast<CoreVM::Runner::Value>(arg) };
        auto const result = runner.invoke(args);
        if (!result.has_value())
            return {};
        auto const* str = reinterpret_cast<CoreVM::CoreString const*>(*result);
        return str != nullptr ? *str : std::string {};
    }

#if !defined(_WIN32)
    /// Write end of the self-pipe a delivered SIGINT pokes. Namespace-scope
    /// because a signal handler cannot carry state; `volatile sig_atomic_t` is the
    /// only type a handler may portably touch.
    volatile std::sig_atomic_t interruptWriteFd = -1;

    /// Records a Ctrl+C by writing one byte to the self-pipe.
    ///
    /// A handler may only call async-signal-safe functions, which rules out
    /// touching the loop directly: `EventLoop::post` takes a mutex and allocates a
    /// `std::function`, so a signal arriving while the loop thread already holds
    /// that mutex would deadlock. `write(2)` is on the safe list, so the handler
    /// just pokes the pipe and the watcher coroutine below does the real work on
    /// the loop thread.
    /// @param signum The delivered signal (unused; only SIGINT is installed).
    extern "C" void onServerInterrupt(int signum)
    {
        (void) signum;
        auto const fd = static_cast<int>(interruptWriteFd);
        if (fd < 0)
            return;
        auto const one = char { 1 };
        // Nothing useful to do if this fails, and errno must not be clobbered.
        auto const savedErrno = errno;
        std::ignore = ::write(fd, &one, 1);
        errno = savedErrno;
    }
#endif

    /// Parks on the self-pipe's read end and stops @p loop once a byte arrives.
    /// @param loop The loop to stop (a pointer, since coroutine reference
    ///        parameters dangle once the coroutine suspends).
    /// @param readFd The self-pipe's read end.
    coro::Task<void> watchForInterrupt(net::EventLoop* loop, net::NativeHandle readFd)
    {
        co_await loop->waitReadable(readFd);
        loop->requestStop();
    }

    /// Routes SIGINT to @p loop for as long as it is alive, restoring the previous
    /// disposition on destruction so the shell's own Ctrl+C handling is unaffected
    /// once the server returns.
    ///
    /// POSIX only: Ctrl+C on Windows arrives on a console-handler thread rather
    /// than as a signal, so wiring it up there needs SetConsoleCtrlHandler and is
    /// left for when the shell itself grows that path. Construction is also a no-op
    /// if the pipe cannot be created — losing Ctrl+C beats failing to serve.
    class ScopedInterruptRedirect
    {
      public:
        /// @param loop The loop to stop when SIGINT arrives.
        explicit ScopedInterruptRedirect([[maybe_unused]] net::EventLoop& loop)
        {
#if !defined(_WIN32)
            auto pipe = net::createSystemPipe();
            if (!pipe)
                return;
            _pipe = std::move(*pipe);
            interruptWriteFd = static_cast<std::sig_atomic_t>(_pipe->writeFd());
            _previous = std::signal(SIGINT, &onServerInterrupt);
            loop.spawn(watchForInterrupt(&loop, _pipe->readFd()));
#endif
        }

        ~ScopedInterruptRedirect()
        {
#if !defined(_WIN32)
            if (_previous != SIG_ERR)
                std::signal(SIGINT, _previous);
            interruptWriteFd = -1;
#endif
        }

        ScopedInterruptRedirect(ScopedInterruptRedirect const&) = delete;
        ScopedInterruptRedirect& operator=(ScopedInterruptRedirect const&) = delete;
        ScopedInterruptRedirect(ScopedInterruptRedirect&&) = delete;
        ScopedInterruptRedirect& operator=(ScopedInterruptRedirect&&) = delete;

      private:
        std::unique_ptr<net::SystemPipe> _pipe; ///< Wakes the loop from the handler.
        void (*_previous)(int) = SIG_ERR;       ///< Disposition to restore.
    };
} // namespace

void Shell::builtinHttpServe(CoreVM::Params& context)
{
    auto const port = static_cast<std::uint16_t>(context.getInt(1));
    auto const* handler = context.getFunction(2);
    if (handler == nullptr)
    {
        error("httpServe: handler is not a function");
        context.setResult(static_cast<CoreVM::CoreNumber>(1));
        return;
    }

    // The server drives its own reactor rather than the shell's TUI runtime:
    // makeDefaultEventSource picks epoll/kqueue where available and falls back to
    // poll, so a wait costs O(ready) rather than O(registered).
    auto source = net::makeDefaultEventSource();
    auto loop = net::EventLoop { *source };

    auto listener = net::listen(loop, "127.0.0.1", port);
    if (!listener.has_value())
    {
        error("httpServe: {}", listener.error().toString());
        context.setResult(static_cast<CoreVM::CoreNumber>(1));
        return;
    }

    // Ctrl+C stops the server: the handler requests a stop on the loop, which
    // cancels the root flow and unwinds the accept loop (a parked accept resolves
    // with Cancelled).
    auto const interruptRedirect = ScopedInterruptRedirect { loop };

    auto* const globals = &_globals;
    auto requestHandler = [handler, globals](net::HttpRequest const& request) {
        return net::HttpResponse::ok(invokeHandler(handler, globals, request.path));
    };

    auto serveFlow = net::serve(listener->get(), std::move(requestHandler));
    try
    {
        loop.blockOn(std::move(serveFlow));
    }
    catch (coro::OperationCancelled const&)
    {
        // Interrupted (Ctrl+C): a clean shutdown, not an error. Close the listener
        // so the OS port is released before we return.
        listener->get()->close();
    }

    context.setResult(static_cast<CoreVM::CoreNumber>(0));
}

} // namespace endo
