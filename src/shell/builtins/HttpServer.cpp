// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <tui/runtime/PollEventSource.hpp>
#include <tui/runtime/TuiRuntime.hpp>

#include <CoreVM/CoreVM.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include <coro/Task.hpp>
#include <net/HttpServer.hpp>
#include <net/Sockets.hpp>

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

    auto source = tui::runtime::PollEventSource {};
    auto runtime = tui::runtime::TuiRuntime { source };

    auto listener = endo::net::listen(runtime, "127.0.0.1", port);
    if (!listener.has_value())
    {
        error("httpServe: {}", listener.error().toString());
        context.setResult(static_cast<CoreVM::CoreNumber>(1));
        return;
    }

    // Ctrl+C stops the server: the interrupt cancels the root flow, which unwinds
    // the accept loop (a parked accept resolves with Cancelled).
    auto* const globals = &_globals;
    auto requestHandler = [handler, globals](endo::net::HttpRequest const& request) {
        return endo::net::HttpResponse::ok(invokeHandler(handler, globals, request.path));
    };

    auto serveFlow = endo::net::serve(listener->get(), std::move(requestHandler));
    try
    {
        runtime.blockOn(std::move(serveFlow));
    }
    catch (endo::coro::OperationCancelled const&)
    {
        // Interrupted (Ctrl+C): a clean shutdown, not an error. Close the listener
        // so the OS port is released before we return.
        listener->get()->close();
    }

    context.setResult(static_cast<CoreVM::CoreNumber>(0));
}

} // namespace endo
