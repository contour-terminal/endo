// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Extracts an error message from an OpenAI-style JSON error response body.
///
/// Parses the body as JSON and looks for `error.message`. Falls back to
/// a generic "HTTP <statusCode>" string if parsing fails.
///
/// @param statusCode The HTTP response status code (used for fallback message).
/// @param body       The raw HTTP response body.
/// @return The extracted error message string.
[[nodiscard]] auto extractJsonErrorMessage(long statusCode, std::string const& body) -> std::string;

/// Maps an HTTP status code and pre-extracted message to a ProviderError.
///
/// Status code mapping:
/// - 401       -> AuthenticationError
/// - 403       -> AuthenticationError (only when @p treat403AsAuth is true)
/// - 429       -> RateLimitError
/// - 500+      -> ServerError
/// - otherwise -> Unknown
///
/// @param statusCode    The HTTP response status code.
/// @param message       The error message to include.
/// @param treat403AsAuth Whether to treat HTTP 403 as an authentication error (default: false).
/// @return The corresponding ProviderError.
[[nodiscard]] auto mapHttpStatusToProviderError(long statusCode,
                                                std::string message,
                                                bool treat403AsAuth = false) -> ProviderError;

} // namespace endo::agent
