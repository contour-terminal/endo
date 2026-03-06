// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

#include <agent/conversation/SessionManager.hpp>

namespace endo::agent
{

SessionManager::SessionManager(std::filesystem::path projectDir):
    _projectDir(std::move(projectDir)),
    _sessionsDir(_projectDir / "sessions"),
    _lastMarkerPath(_sessionsDir / ".last")
{
}

auto SessionManager::sessionPath(std::string_view name) const -> std::filesystem::path
{
    return _sessionsDir / (std::string(name) + ".json");
}

auto SessionManager::listSessions() const -> std::expected<std::vector<SessionMetadata>, HistoryStoreError>
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(_sessionsDir, ec))
        return std::vector<SessionMetadata> {};

    auto sessions = std::vector<SessionMetadata> {};
    for (auto const& entry: std::filesystem::directory_iterator(_sessionsDir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        auto const store = ConversationHistoryStore(entry.path());
        auto metaResult = store.loadMetadataOnly();
        if (!metaResult.has_value())
            continue;

        auto& meta = metaResult.value();
        // Ensure the name matches the file stem.
        if (meta.name.empty())
            meta.name = entry.path().stem().string();

        sessions.push_back(std::move(meta));
    }

    // Sort by updatedAt descending (most recent first).
    std::ranges::sort(sessions, [](auto const& a, auto const& b) { return a.updatedAt > b.updatedAt; });

    return sessions;
}

auto SessionManager::saveSession(std::string_view name,
                                 std::span<ChatMessage const> messages,
                                 SessionMetadata const& metadata) const
    -> std::expected<void, HistoryStoreError>
{
    auto const store = ConversationHistoryStore(sessionPath(name));
    return store.save(messages, metadata);
}

auto SessionManager::loadSession(std::string_view name) const
    -> std::expected<std::pair<SessionMetadata, std::vector<ChatMessage>>, HistoryStoreError>
{
    auto const path = sessionPath(name);
    auto ec = std::error_code {};
    if (!std::filesystem::exists(path, ec))
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Session not found: " + std::string(name),
        });
    }

    auto const store = ConversationHistoryStore(path);
    return store.loadWithMetadata();
}

auto SessionManager::removeSession(std::string_view name) const -> std::expected<void, HistoryStoreError>
{
    auto const store = ConversationHistoryStore(sessionPath(name));
    return store.remove();
}

auto SessionManager::lastActiveSession() const -> std::string
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(_lastMarkerPath, ec))
        return {};

    auto ifs = std::ifstream(_lastMarkerPath);
    if (!ifs.is_open())
        return {};

    auto name = std::string {};
    std::getline(ifs, name);
    return name;
}

void SessionManager::setLastActiveSession(std::string_view name) const
{
    auto ec = std::error_code {};
    std::filesystem::create_directories(_sessionsDir, ec);

    auto ofs = std::ofstream(_lastMarkerPath);
    if (ofs.is_open())
        ofs << name;
}

void SessionManager::clearLastActiveSession() const
{
    auto ec = std::error_code {};
    std::filesystem::remove(_lastMarkerPath, ec);
}

auto SessionManager::sessionExists(std::string_view name) const -> bool
{
    auto ec = std::error_code {};
    return std::filesystem::exists(sessionPath(name), ec);
}

auto SessionManager::generateSessionName(std::string_view text) const -> std::string
{
    // Take first ~30 characters.
    auto const maxLen = std::min(text.size(), size_t { 30 });
    auto slug = std::string {};
    slug.reserve(maxLen);

    auto lastWasHyphen = true; // Prevent leading hyphen.
    for (size_t i = 0; i < maxLen; ++i)
    {
        auto const ch = text[i];
        if (std::isalnum(static_cast<unsigned char>(ch)))
        {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            lastWasHyphen = false;
        }
        else if (!lastWasHyphen)
        {
            slug += '-';
            lastWasHyphen = true;
        }
    }

    // Trim trailing hyphen.
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();

    // Fallback for empty or all-special-characters input.
    if (slug.empty())
        slug = "session";

    // Deduplicate with suffix.
    auto candidate = slug;
    auto suffix = 2;
    while (sessionExists(candidate))
    {
        candidate = slug + "-" + std::to_string(suffix);
        ++suffix;
    }

    return candidate;
}

auto SessionManager::renameSession(std::string_view oldName, std::string_view newName) const
    -> std::expected<void, HistoryStoreError>
{
    auto const oldPath = sessionPath(oldName);
    auto const newPath = sessionPath(newName);
    auto ec = std::error_code {};

    if (!std::filesystem::exists(oldPath, ec))
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Session not found: " + std::string(oldName),
        });

    if (std::filesystem::exists(newPath, ec))
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Session already exists: " + std::string(newName),
        });

    std::filesystem::rename(oldPath, newPath, ec);
    if (ec)
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to rename session file: " + ec.message(),
        });

    // Load the session, update the name in metadata, and re-save.
    auto const store = ConversationHistoryStore(newPath);
    auto loaded = store.loadWithMetadata();
    if (loaded.has_value())
    {
        auto& [meta, messages] = *loaded;
        meta.name = std::string(newName);
        (void) store.save(messages, meta); // NOLINT(bugprone-unused-return-value)
    }

    // Update the .last marker if it pointed to the old name.
    if (lastActiveSession() == oldName)
        setLastActiveSession(newName);

    return {};
}

auto SessionManager::sessionNames() const -> std::vector<std::string>
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(_sessionsDir, ec))
        return {};

    auto names = std::vector<std::string> {};
    for (auto const& entry: std::filesystem::directory_iterator(_sessionsDir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        names.push_back(entry.path().stem().string());
    }

    std::ranges::sort(names);
    return names;
}

} // namespace endo::agent
