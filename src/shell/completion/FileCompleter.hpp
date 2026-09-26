// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

#include <core/platform/FileSystem.hpp>
#include <core/platform/ProcessEnvironment.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider for file and directory paths.
class FileCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a file completer.
    /// @param env Environment abstraction used to resolve the user's home directory
    ///            for tilde (`~`) expansion.
    /// @param fs  Filesystem to enumerate. Completion must offer what the shell will act
    ///            on, so it reads the same filesystem the builtins do.
    FileCompleter(core::platform::ProcessEnvironment const& env, core::platform::FileSystem const& fs);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 50; }

  private:
    core::platform::ProcessEnvironment const& _env;
    core::platform::FileSystem const& _fs;

    /// @brief Expands tilde to home directory.
    [[nodiscard]] std::filesystem::path expandTilde(std::string_view path) const;

    /// @brief Checks if a filename is hidden (starts with dot).
    [[nodiscard]] static bool isHidden(std::string_view name);

    /// @brief Lists entries of @p dir (in @ref _fs) matching @p prefix.
    [[nodiscard]] std::vector<CompletionItem> listDirectory(std::filesystem::path const& dir,
                                                            std::string_view prefix,
                                                            std::string_view pathPrefix) const;
};

} // namespace endo
