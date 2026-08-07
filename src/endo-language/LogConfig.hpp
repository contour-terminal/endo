// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <crispy/LogStore.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace endo::log
{

/// Stores log patterns and checks if categories should be enabled.
/// This solves the problem of lazy category initialization with C++20 modules.
class Config
{
  public:
    /// Returns the singleton instance.
    [[nodiscard]] static Config& instance() noexcept
    {
        static Config config;
        return config;
    }

    /// Sets the log patterns from a comma-separated string.
    void setPatterns(std::string_view patterns)
    {
        _patterns.clear();

        size_t start = 0;
        while (start < patterns.size())
        {
            auto const end = patterns.find(',', start);
            auto pattern = patterns.substr(start, end == std::string_view::npos ? end : end - start);

            // Trim whitespace
            while (!pattern.empty() && pattern.front() == ' ')
                pattern.remove_prefix(1);
            while (!pattern.empty() && pattern.back() == ' ')
                pattern.remove_suffix(1);

            if (!pattern.empty())
                _patterns.emplace_back(pattern);

            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
    }

    /// Checks if a category with the given name should be enabled.
    [[nodiscard]] bool shouldEnable(std::string_view name) const noexcept
    {
        if (_patterns.empty())
            return false;

        for (auto const& pattern: _patterns)
        {
            if (pattern == "*")
                return true;

            if (pattern.ends_with(".*"))
            {
                auto const prefix = std::string_view(pattern).substr(0, pattern.size() - 2);
                if (name.starts_with(prefix))
                    return true;
            }
            else if (pattern.starts_with("*"))
            {
                auto const suffix = std::string_view(pattern).substr(1);
                if (name.ends_with(suffix))
                    return true;
            }
            else if (name == pattern)
            {
                return true;
            }
        }

        return false;
    }

  private:
    Config() = default;
    std::vector<std::string> _patterns;
};

/// Determines the initial state for a log category based on configured patterns.
[[nodiscard]] inline auto categoryState(std::string_view name, bool defaultEnabled = false) noexcept
{
    if (Config::instance().shouldEnable(name))
        return logstore::Category::State::Enabled;
    return defaultEnabled ? logstore::Category::State::Enabled : logstore::Category::State::Disabled;
}

} // namespace endo::log
