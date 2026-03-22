// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

#include <platform/EnvironmentProvider.hpp>

namespace endo::platform
{

/// @brief POSIX implementation of EnvironmentProvider using real OS calls.
///
/// Uses setenv/getenv/unsetenv for environment variables and chdir/getcwd
/// for working directory operations.
class PosixEnvironmentProvider final: public EnvironmentProvider
{
  public:
    /// Returns the singleton instance.
    [[nodiscard]] static PosixEnvironmentProvider& instance();

    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;
    [[nodiscard]] std::vector<std::string> keys() const override;

    [[nodiscard]] std::expected<void, PlatformError> changeDirectory(
        std::filesystem::path const& path) override;
    [[nodiscard]] std::string currentDirectory() const override;

  private:
    std::map<std::string, std::string> _values;
};

} // namespace endo::platform

// Backward-compatible alias
namespace endo
{
using endo::platform::PosixEnvironmentProvider;
} // namespace endo
