// SPDX-License-Identifier: Apache-2.0
#include "PersistentHistory.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

namespace endo
{

PersistentHistory::PersistentHistory(size_t maxSize): _filePath(defaultHistoryPath()), _maxSize(maxSize)
{
    _richEntries.reserve(std::min(maxSize, size_t { 256 }));
}

std::filesystem::path PersistentHistory::defaultHistoryPath()
{
#if defined(_WIN32)
    if (auto const* localAppData = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(localAppData) / "endo" / "state" / "history.yml";
    return std::filesystem::path("history.yml");
#else
    if (auto const* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local" / "state" / "endo" / "history.yml";
    return std::filesystem::path("history.yml");
#endif
}

void PersistentHistory::setFilePath(std::filesystem::path path)
{
    _filePath = std::move(path);
}

void PersistentHistory::load()
{
    if (!std::filesystem::exists(_filePath))
        return;

    try
    {
        auto const root = YAML::LoadFile(_filePath.string());
        if (!root["entries"])
            return;

        auto const entriesNode = root["entries"];
        if (!entriesNode.IsSequence())
            return;

        _richEntries.clear();
        _richEntries.reserve(entriesNode.size());

        for (auto const& node: entriesNode)
        {
            if (!node["cmd"])
                continue;

            auto entry = HistoryEntry {};
            entry.command = node["cmd"].as<std::string>();

            if (node["ts"])
            {
                auto const ts = node["ts"].as<int64_t>();
                entry.lastExecuted = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(ts));
            }
            else
            {
                entry.lastExecuted = std::chrono::system_clock::now();
            }

            if (node["count"])
                entry.executionCount = node["count"].as<uint32_t>();

            entry.persisted = true;
            _richEntries.push_back(std::move(entry));
        }

        rebuildEntriesCache();
    }
    catch (YAML::Exception const&)
    {
        // Corrupt YAML — start with empty history
        _richEntries.clear();
        _entries.clear();
    }
}

void PersistentHistory::autoImportIfEmpty()
{
    // Only import if our history file does not exist
    if (std::filesystem::exists(_filePath))
        return;

    // Try fish first, then zsh, then bash
#if !defined(_WIN32)
    auto const* home = std::getenv("HOME");
    if (!home)
        return;

    auto const homePath = std::filesystem::path(home);

    // Fish
    auto const fishPath = homePath / ".local" / "share" / "fish" / "fish_history";
    if (std::filesystem::exists(fishPath))
    {
        if (importFish(fishPath) > 0)
        {
            _dirty = true;
            flush();
            return;
        }
    }

    // Zsh
    auto zshPath = std::filesystem::path {};
    if (auto const* histfile = std::getenv("HISTFILE"))
        zshPath = histfile;
    else
        zshPath = homePath / ".zsh_history";

    if (std::filesystem::exists(zshPath))
    {
        if (importZsh(zshPath) > 0)
        {
            _dirty = true;
            flush();
            return;
        }
    }

    // Bash
    auto bashPath = std::filesystem::path {};
    if (auto const* histfile = std::getenv("HISTFILE"))
        bashPath = histfile;
    else
        bashPath = homePath / ".bash_history";

    if (std::filesystem::exists(bashPath))
    {
        if (importBash(bashPath) > 0)
        {
            _dirty = true;
            flush();
            return;
        }
    }
#endif
}

void PersistentHistory::add(std::string entry)
{
    if (entry.empty())
        return;

    // Search for existing entry
    auto const it =
        std::ranges::find_if(_richEntries, [&entry](auto const& e) { return e.command == entry; });

    if (it != _richEntries.end())
    {
        // Update existing entry
        it->executionCount++;
        it->lastExecuted = std::chrono::system_clock::now();

        // Move to end (most recent)
        auto updated = std::move(*it);
        _richEntries.erase(it);
        _richEntries.push_back(std::move(updated));
        _lastAddedIndex = _richEntries.size() - 1;
    }
    else
    {
        // New entry — not persisted until markLastResult(0)
        evictIfNeeded();

        _richEntries.push_back(HistoryEntry {
            .command = std::move(entry),
            .lastExecuted = std::chrono::system_clock::now(),
            .executionCount = 1,
            .persisted = false,
        });
        _lastAddedIndex = _richEntries.size() - 1;
    }

    rebuildEntriesCache();
}

void PersistentHistory::markLastResult(int exitCode)
{
    if (!_lastAddedIndex || *_lastAddedIndex >= _richEntries.size())
        return;

    auto& entry = _richEntries[*_lastAddedIndex];

    if (exitCode == 0)
    {
        entry.persisted = true;
        _dirty = true;
        flush();
    }
    // Non-zero: entry stays in session but persisted flag unchanged
    // (false for new commands, true for previously saved ones)
}

std::vector<std::string> const& PersistentHistory::entries() const
{
    return _entries;
}

size_t PersistentHistory::size() const
{
    return _richEntries.size();
}

size_t PersistentHistory::maxSize() const
{
    return _maxSize;
}

void PersistentHistory::clear()
{
    _richEntries.clear();
    _entries.clear();
    _lastAddedIndex.reset();
    _dirty = true;
    flush();
}

std::vector<std::string_view> PersistentHistory::search(std::string_view prefix, size_t maxResults) const
{
    auto results = std::vector<std::string_view> {};
    results.reserve(std::min(maxResults, _richEntries.size()));

    // Search from newest to oldest (reverse order)
    for (auto it = _richEntries.rbegin(); it != _richEntries.rend() && results.size() < maxResults; ++it)
    {
        if (tui::SmartCaseMatch::matchesPrefix(it->command, prefix))
        {
            // Entries are unique in _richEntries, no dedup needed
            results.emplace_back(it->command);
        }
    }

    return results;
}

std::vector<History::FuzzySearchResult> PersistentHistory::searchFuzzy(std::string_view prefix,
                                                                       size_t maxResults) const
{
    auto results = std::vector<FuzzySearchResult> {};
    results.reserve(std::min(maxResults * 2, _richEntries.size()));

    auto fuzzyConfig = tui::FuzzyConfig {};
    auto const minThreshold = fuzzyConfig.minMatchThreshold;

    // Collect all matches from newest to oldest
    auto recencyBonus = static_cast<int>(_richEntries.size());
    for (auto it = _richEntries.rbegin(); it != _richEntries.rend(); ++it, --recencyBonus)
    {
        // Check prefix match first
        auto const isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(it->command, prefix);
        auto fuzzyResult = tui::FuzzyMatchResult {};
        auto isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(it->command, prefix);
            auto const textLen = tui::FuzzyMatch::countGraphemes(it->command);
            isFuzzyMatch = fuzzyResult.matches && fuzzyResult.quality(textLen) >= minThreshold;
        }

        if (!isPrefixMatch && !isFuzzyMatch)
            continue;

        // Frequency bonus: min(executionCount * 5, 200)
        auto const frequencyBonus =
            static_cast<int>(std::min(static_cast<uint32_t>(it->executionCount * 5), uint32_t { 200 }));

        auto score = 0;
        auto matchPositions = std::vector<size_t> {};

        if (isPrefixMatch)
        {
            score = tui::SmartCaseMatch::adjustScore(100, it->command, prefix);
            score += fuzzyConfig.prefixMatchBonus + recencyBonus + frequencyBonus;
        }
        else
        {
            score = tui::FuzzyMatch::calculateScore(50, it->command, prefix, fuzzyResult, fuzzyConfig);
            score += recencyBonus + frequencyBonus;
            matchPositions = std::move(fuzzyResult.positions);
        }

        results.push_back(FuzzySearchResult { .entry = it->command,
                                              .positions = std::move(matchPositions),
                                              .score = score,
                                              .isPrefixMatch = isPrefixMatch });
    }

    // Sort by score descending
    std::ranges::sort(results, [](auto const& a, auto const& b) { return a.score > b.score; });

    // Trim to maxResults
    if (results.size() > maxResults)
        results.resize(maxResults);

    return results;
}

void PersistentHistory::flush()
{
    if (!_dirty)
        return;

    // Create parent directories
    auto const parentDir = _filePath.parent_path();
    if (!parentDir.empty())
        std::filesystem::create_directories(parentDir);

    // Serialize to YAML
    auto emitter = YAML::Emitter {};
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "version" << YAML::Value << 1;
    emitter << YAML::Key << "entries" << YAML::Value << YAML::BeginSeq;

    for (auto const& entry: _richEntries)
    {
        if (!entry.persisted)
            continue;

        auto const ts =
            std::chrono::duration_cast<std::chrono::seconds>(entry.lastExecuted.time_since_epoch()).count();

        emitter << YAML::BeginMap;
        emitter << YAML::Key << "cmd" << YAML::Value << entry.command;
        emitter << YAML::Key << "ts" << YAML::Value << ts;
        emitter << YAML::Key << "count" << YAML::Value << entry.executionCount;
        emitter << YAML::EndMap;
    }

    emitter << YAML::EndSeq;
    emitter << YAML::EndMap;

    // Atomic write: write to .tmp, then rename
    auto const tmpPath = std::filesystem::path(_filePath.string() + ".tmp");
    {
        auto ofs = std::ofstream(tmpPath);
        if (!ofs)
            return;
        ofs << emitter.c_str() << '\n';
    }

    std::filesystem::rename(tmpPath, _filePath);
    _dirty = false;
}

void PersistentHistory::rebuildEntriesCache()
{
    _entries.clear();
    _entries.reserve(_richEntries.size());
    for (auto const& entry: _richEntries)
        _entries.push_back(entry.command);
}

void PersistentHistory::evictIfNeeded()
{
    if (_richEntries.size() < _maxSize)
        return;

    // Remove the oldest persisted entry with the lowest execution count
    auto worstIt = _richEntries.end();
    auto worstScore = std::numeric_limits<int64_t>::max();

    for (auto it = _richEntries.begin(); it != _richEntries.end(); ++it)
    {
        // Combine recency (index) and frequency for eviction scoring
        auto const age = static_cast<int64_t>(std::distance(_richEntries.begin(), it));
        auto const score = static_cast<int64_t>(it->executionCount) * 1000 + age;
        if (score < worstScore)
        {
            worstScore = score;
            worstIt = it;
        }
    }

    if (worstIt != _richEntries.end())
        _richEntries.erase(worstIt);
}

size_t PersistentHistory::importFish(std::filesystem::path const& path)
{
    auto ifs = std::ifstream(path);
    if (!ifs)
        return 0;

    auto commands = std::vector<std::pair<std::string, std::time_t>> {};
    auto currentCmd = std::string {};
    auto currentTs = std::time_t { 0 };

    auto line = std::string {};
    while (std::getline(ifs, line))
    {
        if (line.starts_with("- cmd: "))
        {
            // Save previous command
            if (!currentCmd.empty())
                commands.emplace_back(std::move(currentCmd), currentTs);

            currentCmd = line.substr(7); // len("- cmd: ") == 7
            currentTs = 0;
        }
        else if (line.starts_with("  when: "))
        {
            try
            {
                currentTs = std::stol(line.substr(8)); // len("  when: ") == 8
            }
            catch (...)
            {
                currentTs = 0;
            }
        }
    }
    // Don't forget the last entry
    if (!currentCmd.empty())
        commands.emplace_back(std::move(currentCmd), currentTs);

    // Import most recent 1000
    auto const startIdx = commands.size() > 1000 ? commands.size() - 1000 : size_t { 0 };

    auto imported = size_t { 0 };
    for (auto i = startIdx; i < commands.size(); ++i)
    {
        auto& [cmd, ts] = commands[i];
        if (cmd.empty())
            continue;

        // Dedup
        auto const exists =
            std::ranges::any_of(_richEntries, [&cmd](auto const& e) { return e.command == cmd; });
        if (exists)
            continue;

        _richEntries.push_back(HistoryEntry {
            .command = std::move(cmd),
            .lastExecuted =
                ts > 0 ? std::chrono::system_clock::from_time_t(ts) : std::chrono::system_clock::now(),
            .executionCount = 1,
            .persisted = true,
        });
        ++imported;
    }

    rebuildEntriesCache();
    return imported;
}

size_t PersistentHistory::importZsh(std::filesystem::path const& path)
{
    auto ifs = std::ifstream(path);
    if (!ifs)
        return 0;

    auto commands = std::vector<std::pair<std::string, std::time_t>> {};
    auto line = std::string {};

    while (std::getline(ifs, line))
    {
        if (line.empty())
            continue;

        auto cmd = std::string {};
        auto ts = std::time_t { 0 };

        // Extended format: ": timestamp:duration;command"
        if (line.starts_with(": "))
        {
            auto const semicolon = line.find(';');
            if (semicolon != std::string::npos && semicolon + 1 < line.size())
            {
                cmd = line.substr(semicolon + 1);
                try
                {
                    // Parse timestamp between ": " and ":"
                    auto const colon = line.find(':', 2);
                    if (colon != std::string::npos)
                        ts = std::stol(line.substr(2, colon - 2));
                }
                catch (...)
                {
                    ts = 0;
                }
            }
        }
        else
        {
            // Plain text format
            cmd = line;
        }

        if (!cmd.empty())
            commands.emplace_back(std::move(cmd), ts);
    }

    // Import most recent 1000
    auto const startIdx = commands.size() > 1000 ? commands.size() - 1000 : size_t { 0 };

    auto imported = size_t { 0 };
    for (auto i = startIdx; i < commands.size(); ++i)
    {
        auto& [cmd, ts] = commands[i];
        if (cmd.empty())
            continue;

        auto const exists =
            std::ranges::any_of(_richEntries, [&cmd](auto const& e) { return e.command == cmd; });
        if (exists)
            continue;

        _richEntries.push_back(HistoryEntry {
            .command = std::move(cmd),
            .lastExecuted =
                ts > 0 ? std::chrono::system_clock::from_time_t(ts) : std::chrono::system_clock::now(),
            .executionCount = 1,
            .persisted = true,
        });
        ++imported;
    }

    rebuildEntriesCache();
    return imported;
}

size_t PersistentHistory::importBash(std::filesystem::path const& path)
{
    auto ifs = std::ifstream(path);
    if (!ifs)
        return 0;

    auto commands = std::vector<std::string> {};
    auto line = std::string {};

    while (std::getline(ifs, line))
    {
        // Skip timestamp lines (bash HISTTIMEFORMAT)
        if (line.starts_with('#'))
            continue;
        if (!line.empty())
            commands.push_back(std::move(line));
    }

    // Import most recent 1000
    auto const startIdx = commands.size() > 1000 ? commands.size() - 1000 : size_t { 0 };

    auto imported = size_t { 0 };
    for (auto i = startIdx; i < commands.size(); ++i)
    {
        auto& cmd = commands[i];
        if (cmd.empty())
            continue;

        auto const exists =
            std::ranges::any_of(_richEntries, [&cmd](auto const& e) { return e.command == cmd; });
        if (exists)
            continue;

        _richEntries.push_back(HistoryEntry {
            .command = std::move(cmd),
            .lastExecuted = std::chrono::system_clock::now(),
            .executionCount = 1,
            .persisted = true,
        });
        ++imported;
    }

    rebuildEntriesCache();
    return imported;
}

} // namespace endo
