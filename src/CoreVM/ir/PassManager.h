// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <functional>
#include <list>
#include <memory>
#include <utility>

namespace CoreVM
{

class IRHandler;
class IRProgram;

class PassManager
{
  public:
    using HandlerPass = std::function<bool(IRHandler* handler)>;

    PassManager() = default;
    ~PassManager() = default;

    /** registers given pass to the pass manager.
     *
     * @param name uniquely identifyable name of the handler pass
     * @param handler callback to invoke to handle the transformation pass
     *
     * The handler must return @c true if it modified its input, @c false otherwise.
     */
    void registerPass(std::string name, HandlerPass handlerPass);

    /** runs passes on a complete program.
     */
    void run(IRProgram* program);

    /** runs passes on given handler.
     */
    void run(IRHandler* handler);

    template <typename... Args>
    void logDebug(fmt::format_string<Args...> msg, Args... args)
    {
        logDebug(fmt::vformat(msg, fmt::make_format_args(args...)));
    }

    void logDebug(const std::string& msg);

  private:
    std::list<std::pair<std::string, HandlerPass>> _handlerPasses;
};

} // namespace CoreVM
