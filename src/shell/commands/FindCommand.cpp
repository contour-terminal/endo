// SPDX-License-Identifier: Apache-2.0
#include "FindCommand.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <filesystem>
#include <ranges>

#include <platform/PathUtils.hpp>

namespace endo
{

FindCommand::FindCommand(find::FindOptions options, std::unique_ptr<find::Expr> expression):
    _options(std::move(options)), _expression(std::move(expression))
{
}

uint16_t FindCommand::outputTypeId() const
{
    return CoreVM::BuiltinTypeId::FileInfo;
}

CoreVM::TypedObject* FindCommand::execute(CoreVM::Runner& runner) const
{
    namespace fs = std::filesystem;

    // Collect all matching entries first, so we can build the list right-to-left
    struct MatchedEntry
    {
        std::string path;
        uintmax_t size;
        uint64_t mode;
        int64_t mtime;
        bool isDir;
    };

    std::vector<MatchedEntry> matches;

    for (auto const& searchPath: _options.searchPaths)
    {
        std::error_code ec;
        auto const canonicalSearch = fs::path(searchPath);

        // Check if the search path itself matches (depth 0)
        if (!_options.minDepth.has_value() || _options.minDepth.value() <= 0)
        {
            auto const status = fs::symlink_status(canonicalSearch, ec);
            if (!ec)
            {
                find::FindEntry entry {
                    .path = canonicalSearch,
                    .filename = canonicalSearch.filename().string(),
                    .type = status.type(),
                    .size = fs::is_regular_file(status) ? fs::file_size(canonicalSearch, ec) : 0,
                    .mtime = fs::last_write_time(canonicalSearch, ec),
                    .depth = 0,
                };
                if (!_expression || _expression->evaluate(entry))
                {
                    auto const fileStatus = fs::status(canonicalSearch, ec);
                    matches.push_back({
                        .path = platform::normalizePath(canonicalSearch),
                        .size = entry.size,
                        .mode = static_cast<uint64_t>(fileStatus.permissions()),
                        .mtime = static_cast<int64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(entry.mtime.time_since_epoch())
                                .count()),
                        .isDir = (entry.type == fs::file_type::directory),
                    });
                }
            }
        }

        // Skip recursion if maxdepth is 0
        if (_options.maxDepth.has_value() && _options.maxDepth.value() == 0)
            continue;

        auto dirIter = fs::recursive_directory_iterator(
            canonicalSearch, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            continue;

        for (auto const& dirEntry: dirIter)
        {
            auto const depth =
                dirIter.depth()
                + 1; // +1 because recursive_directory_iterator depth is 0-based from search root's children

            // Apply maxdepth
            if (_options.maxDepth.has_value() && depth > _options.maxDepth.value())
            {
                dirIter.disable_recursion_pending();
                continue;
            }

            // Apply mindepth (skip matching but continue traversal)
            if (_options.minDepth.has_value() && depth < _options.minDepth.value())
                continue;

            auto const status = dirEntry.symlink_status(ec);
            if (ec)
                continue;

            find::FindEntry entry {
                .path = dirEntry.path(),
                .filename = dirEntry.path().filename().string(),
                .type = status.type(),
                .size = dirEntry.is_regular_file(ec) ? dirEntry.file_size(ec) : 0,
                .mtime = dirEntry.last_write_time(ec),
                .depth = depth,
            };

            if (!_expression || _expression->evaluate(entry))
            {
                auto const fileStatus = dirEntry.status(ec);
                matches.push_back({
                    .path = platform::normalizePath(dirEntry.path()),
                    .size = entry.size,
                    .mode = static_cast<uint64_t>(ec ? fs::perms::none : fileStatus.permissions()),
                    .mtime = static_cast<int64_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(entry.mtime.time_since_epoch())
                            .count()),
                    .isDir = (entry.type == fs::file_type::directory),
                });
            }
        }
    }

    // Build cons-cell list right-to-left (same pattern as LsCommand)
    auto* list = runner.allocObject(CoreVM::BuiltinTypeId::List);
    list->tag = 0; // Nil

    // Hoisted out of the loop: resolving the working directory is a syscall, and `find` can match
    // many thousands of entries.
    auto const workingDirectory = platform::absoluteDirectory({});

    for (auto& match: std::ranges::reverse_view(matches))
    {
        // find's name is the path it walked, which is relative when the search root was; the
        // record additionally carries the absolute form. It reports no symlink information, and
        // the builder writes valid values into those slots regardless, so f.isSymlink / f.target
        // never read back as a null string.
        auto const resolved = platform::absolutePath(match.path, workingDirectory);
        auto* record = endo::builtins::makeFileInfoRecord(&runner,
                                                          { .name = match.path,
                                                            .path = resolved,
                                                            .size = static_cast<int64_t>(match.size),
                                                            .mode = static_cast<int64_t>(match.mode),
                                                            .mtime = match.mtime,
                                                            .isDir = match.isDir });

        auto* cons = runner.allocObject(CoreVM::BuiltinTypeId::List);
        cons->tag = 1; // Cons
        cons->setSlot(0, reinterpret_cast<uintptr_t>(record));
        cons->setSlot(1, reinterpret_cast<uintptr_t>(list));
        list = cons;
    }

    return list;
}

} // namespace endo
