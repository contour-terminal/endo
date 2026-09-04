// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace endo
{

/// @brief Dispatches a query tag to whichever wrapped provider serves it.
///
/// Providers answer `{}` for tags they do not own, so resolution is "ask each in turn and
/// take the first non-empty answer". This keeps tag ownership with the providers instead of
/// with the registration site: a spec declaring a new tag works as soon as some provider
/// serves it, rather than silently completing to nothing because the wrong provider was
/// attached to it.
class CompositeQueryProvider: public CommandQueryProvider
{
  public:
    /// @brief Constructs a provider over @p providers, consulted in order.
    /// @param providers The providers to delegate to; null entries are ignored.
    explicit CompositeQueryProvider(std::vector<std::unique_ptr<CommandQueryProvider>> providers):
        _providers(std::move(providers))
    {
    }

    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override
    {
        for (auto const& provider: _providers)
        {
            if (!provider)
                continue;
            if (auto results = provider->query(queryTag); !results.empty())
                return results;
        }
        return {};
    }

  private:
    std::vector<std::unique_ptr<CommandQueryProvider>> _providers;
};

} // namespace endo
