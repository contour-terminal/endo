// SPDX-License-Identifier: Apache-2.0
#include "ProviderUtils.hpp"

#include <format>
#include <utility>

#include <nlohmann/json.hpp>

namespace endo::agent
{

auto extractJsonErrorMessage(long statusCode, std::string const& body) -> std::string
{
    auto message = std::format("HTTP {}", statusCode);

    auto const parsed = nlohmann::json::parse(body, nullptr, false);
    if (!parsed.is_discarded() && parsed.contains("error") && parsed["error"].contains("message"))
        message = parsed["error"]["message"].get<std::string>();

    return message;
}

auto mapHttpStatusToProviderError(long statusCode, std::string message, bool treat403AsAuth) -> ProviderError
{
    auto code = ProviderErrorCode::Unknown;
    if (statusCode == 401 || (treat403AsAuth && statusCode == 403))
        code = ProviderErrorCode::AuthenticationError;
    else if (statusCode == 429)
        code = ProviderErrorCode::RateLimitError;
    else if (statusCode >= 500)
        code = ProviderErrorCode::ServerError;

    return ProviderError { .code = code,
                           .message = std::move(message),
                           .httpStatus = static_cast<int>(statusCode) };
}

} // namespace endo::agent
