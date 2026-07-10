// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

/// @brief Completion provider for file and directory paths.
class FileCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a file completer.
    /// @param env Environment abstraction used to resolve the user's home directory
    ///            for tilde (`~`) expansion.
    explicit FileCompleter(EnvironmentProvider const& env);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 50; }

  private:
    EnvironmentProvider const& _env;

    /// @brief Expands tilde to home directory.
    [[nodiscard]] std::filesystem::path expandTilde(std::string_view path) const;

    /// @brief Checks if a filename is hidden (starts with dot).
    [[nodiscard]] static bool isHidden(std::string_view name);

    /// @brief Lists directory entries matching prefix.
    [[nodiscard]] static std::vector<CompletionItem> listDirectory(std::filesystem::path const& dir,
                                                                   std::string_view prefix,
                                                                   std::string_view pathPrefix);
};

} // namespace endo
