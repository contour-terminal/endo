// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/platform/EnvironmentProvider.hpp>

#include <map>
#include <string>

namespace endo
{

/// @brief Windows implementation of EnvironmentProvider.
///
/// Uses GetEnvironmentVariable/SetEnvironmentVariable for environment variables
/// and std::filesystem for working directory operations. Environment variable names
/// are treated case-insensitively, matching Windows behavior.
class WindowsEnvironmentProvider final: public EnvironmentProvider
{
  public:
    /// Returns the singleton instance.
    [[nodiscard]] static WindowsEnvironmentProvider& instance();

    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;
    [[nodiscard]] std::vector<std::string> keys() const override;

    [[nodiscard]] std::expected<void, ShellError> changeDirectory(std::filesystem::path const& path) override;
    [[nodiscard]] std::string currentDirectory() const override;

  private:
    /// @brief Internal storage for shell-local variables (case-insensitive key comparison).
    struct CaseInsensitiveLess
    {
        bool operator()(std::string const& a, std::string const& b) const;
    };
    std::map<std::string, std::string, CaseInsensitiveLess> _values;
};

} // namespace endo
