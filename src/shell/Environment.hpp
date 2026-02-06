// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Abstract interface for environment variable access.
class Environment
{
  public:
    virtual ~Environment() = default;

    virtual void set(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual std::optional<std::string_view> get(std::string_view name) const = 0;
    virtual void unset(std::string_view name) = 0;

    virtual void exportVariable(std::string_view name) = 0;

    /// Returns all variable names currently defined (local and exported).
    [[nodiscard]] virtual std::vector<std::string> keys() const = 0;

    void setAndExport(std::string_view name, std::string_view value);
};

/// @brief Test environment for unit testing.
class TestEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;
    [[nodiscard]] std::vector<std::string> keys() const override;

  private:
    std::map<std::string, std::string> _values;
};

/// @brief System environment using actual environment variables.
class SystemEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;
    [[nodiscard]] std::vector<std::string> keys() const override;

    static SystemEnvironment& instance();

  private:
    std::map<std::string, std::string> _values;
};

} // namespace endo
