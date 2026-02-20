// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider for file and directory paths.
class FileCompleter: public CompletionProvider
{
  public:
    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 50; }

  private:
    /// @brief Expands tilde to home directory.
    [[nodiscard]] std::filesystem::path expandTilde(std::string_view path) const;

    /// @brief Escapes special characters for shell.
    [[nodiscard]] static std::string escapeForShell(std::string_view path);

    /// @brief Checks if a filename is hidden (starts with dot).
    [[nodiscard]] static bool isHidden(std::string_view name);

    /// @brief Lists directory entries matching prefix.
    [[nodiscard]] std::vector<CompletionItem> listDirectory(std::filesystem::path const& dir,
                                                            std::string_view prefix,
                                                            std::string_view pathPrefix) const;
};

} // namespace endo
