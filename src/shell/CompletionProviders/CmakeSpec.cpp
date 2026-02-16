// SPDX-License-Identifier: Apache-2.0
#include "CmakeSpec.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace endo
{

namespace
{
    /// @brief Returns the host system name for condition evaluation.
    [[nodiscard]] constexpr std::string_view hostSystemName() noexcept
    {
#if defined(__linux__)
        return "Linux";
#elif defined(__APPLE__)
        return "Darwin";
#elif defined(_WIN32)
        return "Windows";
#else
        return "Unknown";
#endif
    }

    /// @brief Resolves a single CMake preset variable reference.
    /// @param variable The variable name (without ${ and }).
    /// @return The resolved value, or the original ${...} reference if unknown.
    [[nodiscard]] std::string resolveVariable(std::string_view variable)
    {
        if (variable == "hostSystemName")
            return std::string(hostSystemName());
        return std::format("${{{}}}", variable);
    }

    /// @brief Resolves all ${...} variable references in a string.
    [[nodiscard]] std::string resolveVariables(std::string const& input)
    {
        auto result = std::string {};
        auto pos = std::size_t { 0 };
        while (pos < input.size())
        {
            auto const start = input.find("${", pos);
            if (start == std::string::npos)
            {
                result.append(input, pos);
                break;
            }
            auto const end = input.find('}', start + 2);
            if (end == std::string::npos)
            {
                result.append(input, pos);
                break;
            }
            result.append(input, pos, start - pos);
            auto const varName = std::string_view(input).substr(start + 2, end - start - 2);
            result.append(resolveVariable(varName));
            pos = end + 1;
        }
        return result;
    }

    /// @brief Evaluates a CMake preset condition object.
    /// @param condition The JSON condition object.
    /// @return True if the condition passes (preset should be shown), false otherwise.
    [[nodiscard]] bool evaluateCondition(nlohmann::json const& condition)
    {
        if (!condition.is_object() || !condition.contains("type"))
            return true;

        auto const& type = condition["type"];
        if (!type.is_string())
            return true;

        auto const typeStr = type.get<std::string>();

        if (typeStr == "equals")
        {
            if (!condition.contains("lhs") || !condition.contains("rhs"))
                return true;
            auto const lhs = resolveVariables(condition["lhs"].get<std::string>());
            auto const rhs = resolveVariables(condition["rhs"].get<std::string>());
            return lhs == rhs;
        }

        if (typeStr == "notEquals")
        {
            if (!condition.contains("lhs") || !condition.contains("rhs"))
                return true;
            auto const lhs = resolveVariables(condition["lhs"].get<std::string>());
            auto const rhs = resolveVariables(condition["rhs"].get<std::string>());
            return lhs != rhs;
        }

        if (typeStr == "inList")
        {
            if (!condition.contains("string") || !condition.contains("list"))
                return true;
            auto const str = resolveVariables(condition["string"].get<std::string>());
            auto const& list = condition["list"];
            if (!list.is_array())
                return true;
            for (auto const& item: list)
                if (item.is_string() && resolveVariables(item.get<std::string>()) == str)
                    return true;
            return false;
        }

        if (typeStr == "notInList")
        {
            if (!condition.contains("string") || !condition.contains("list"))
                return true;
            auto const str = resolveVariables(condition["string"].get<std::string>());
            auto const& list = condition["list"];
            if (!list.is_array())
                return true;
            for (auto const& item: list)
                if (item.is_string() && resolveVariables(item.get<std::string>()) == str)
                    return false;
            return true;
        }

        if (typeStr == "not")
        {
            if (!condition.contains("condition"))
                return true;
            return !evaluateCondition(condition["condition"]);
        }

        if (typeStr == "anyOf")
        {
            if (!condition.contains("conditions") || !condition["conditions"].is_array())
                return true;
            for (auto const& sub: condition["conditions"])
                if (evaluateCondition(sub))
                    return true;
            return false;
        }

        if (typeStr == "allOf")
        {
            if (!condition.contains("conditions") || !condition["conditions"].is_array())
                return true;
            for (auto const& sub: condition["conditions"])
                if (!evaluateCondition(sub))
                    return false;
            return true;
        }

        // Unrecognized type — permissive: show the preset
        return true;
    }

    /// @brief Collected preset entry from JSON files (before filtering).
    struct PresetEntry
    {
        std::string name;
        std::string displayName;
        bool hidden = false;
        std::optional<nlohmann::json> condition;
        std::vector<std::string> inherits;
    };

    /// @brief Collects all presets from a JSON file and its includes into a map.
    /// @param path Path to the preset file (relative to baseDir).
    /// @param baseDir Directory used to resolve relative paths.
    /// @param allPresets Map to populate: name → PresetEntry.
    /// @param visited Set of canonical paths already processed (cycle guard).
    void collectPresetsFromFile(std::string const& path,
                                std::string const& baseDir,
                                std::unordered_map<std::string, PresetEntry>& allPresets,
                                std::set<std::string>& visited)
    {
        auto const resolvedPath = std::filesystem::path(baseDir) / path;

        auto ec = std::error_code {};
        auto const canonical = std::filesystem::canonical(resolvedPath, ec);
        if (ec)
            return;
        if (!visited.insert(canonical.string()).second)
            return;

        auto file = std::ifstream(canonical);
        if (!file.is_open())
            return;

        auto doc = nlohmann::json {};
        try
        {
            file >> doc;
        }
        catch (nlohmann::json::parse_error const&)
        {
            return;
        }

        static constexpr auto presetKeys = {
            "configurePresets", "buildPresets", "testPresets", "packagePresets", "workflowPresets",
        };

        for (auto const* key: presetKeys)
        {
            if (!doc.contains(key) || !doc[key].is_array())
                continue;

            for (auto const& preset: doc[key])
            {
                if (!preset.contains("name") || !preset["name"].is_string())
                    continue;

                auto entry = PresetEntry {};
                entry.name = preset["name"].get<std::string>();

                if (preset.contains("displayName") && preset["displayName"].is_string())
                    entry.displayName = preset["displayName"].get<std::string>();

                if (preset.contains("hidden") && preset["hidden"].is_boolean())
                    entry.hidden = preset["hidden"].get<bool>();

                if (preset.contains("condition"))
                    entry.condition = preset["condition"];

                if (preset.contains("inherits"))
                {
                    auto const& inherits = preset["inherits"];
                    if (inherits.is_string())
                        entry.inherits.push_back(inherits.get<std::string>());
                    else if (inherits.is_array())
                        for (auto const& parent: inherits)
                            if (parent.is_string())
                                entry.inherits.push_back(parent.get<std::string>());
                }

                // First entry wins (earlier files take priority)
                allPresets.try_emplace(entry.name, std::move(entry));
            }
        }

        // Recursively process "include" array
        if (doc.contains("include") && doc["include"].is_array())
        {
            auto const parentDir = canonical.parent_path().string();
            for (auto const& include: doc["include"])
            {
                if (!include.is_string())
                    continue;
                collectPresetsFromFile(include.get<std::string>(), parentDir, allPresets, visited);
            }
        }
    }

    /// @brief Checks if a preset's condition (own or inherited) is satisfied.
    /// @param name The preset name to check.
    /// @param allPresets Map of all collected presets.
    /// @param checking Set of names currently being checked (cycle guard for inherits).
    /// @return True if the preset's effective condition is satisfied.
    [[nodiscard]] bool isPresetAvailable(std::string const& name,
                                         std::unordered_map<std::string, PresetEntry> const& allPresets,
                                         std::set<std::string>& checking)
    {
        auto const it = allPresets.find(name);
        if (it == allPresets.end())
            return true; // Unknown preset — permissive

        auto const& entry = it->second;

        // If the preset has its own condition, evaluate it directly
        if (entry.condition.has_value())
            return evaluateCondition(*entry.condition);

        // Otherwise, check inherited conditions (all parents must be available)
        if (!checking.insert(name).second)
            return true; // Cycle in inherits — permissive

        for (auto const& parent: entry.inherits)
        {
            if (!isPresetAvailable(parent, allPresets, checking))
                return false;
        }

        checking.erase(name);
        return true;
    }

} // namespace

CommandSpec createCmakeSpec()
{
    auto spec = CommandSpec {};
    spec.command = "cmake";
    spec.description = "Cross-platform build system generator";

    spec.globalOptions = {
        { .longName = "--preset",
          .description = "Configure/build/test preset name",
          .valueKind = OptionValueKind::DynamicQuery,
          .queryTag = "presets" },
        { .longName = "--list-presets", .description = "List available presets" },
        { .longName = "--build", .description = "Build mode" },
        { .longName = "--install", .description = "Install mode" },
        { .longName = "",
          .shortName = "-S",
          .description = "Source directory",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-B",
          .description = "Build directory",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-G",
          .description = "Build system generator",
          .valueKind = OptionValueKind::Enum,
          .enumValues = { "Ninja", "Ninja Multi-Config", "Unix Makefiles", "Watcom WMake" } },
        { .longName = "",
          .shortName = "-D",
          .description = "Set cache variable",
          .valueKind = OptionValueKind::String },
        { .longName = "--parallel",
          .shortName = "-j",
          .description = "Parallel build jobs",
          .valueKind = OptionValueKind::String },
        { .longName = "--target",
          .shortName = "-t",
          .description = "Build target",
          .valueKind = OptionValueKind::String },
        { .longName = "--config",
          .description = "Build configuration",
          .valueKind = OptionValueKind::Enum,
          .enumValues = { "Debug", "Release", "RelWithDebInfo", "MinSizeRel" } },
        { .longName = "--clean-first", .description = "Clean before building" },
        { .longName = "--verbose", .shortName = "-v", .description = "Verbose output" },
        { .longName = "--version", .description = "Print cmake version" },
        { .longName = "--help", .description = "Print help" },
    };

    return spec;
}

CommandSpec createCtestSpec()
{
    auto spec = CommandSpec {};
    spec.command = "ctest";
    spec.description = "CMake test driver";

    spec.globalOptions = {
        { .longName = "--preset",
          .description = "Test preset name",
          .valueKind = OptionValueKind::DynamicQuery,
          .queryTag = "presets" },
        { .longName = "--list-presets", .description = "List available presets" },
        { .longName = "--parallel",
          .shortName = "-j",
          .description = "Parallel test jobs",
          .valueKind = OptionValueKind::String },
        { .longName = "--build-config",
          .shortName = "-C",
          .description = "Build configuration",
          .valueKind = OptionValueKind::Enum,
          .enumValues = { "Debug", "Release", "RelWithDebInfo", "MinSizeRel" } },
        { .longName = "--test-dir", .description = "Test directory", .valueKind = OptionValueKind::Path },
        { .longName = "--output-on-failure", .description = "Show output on test failure" },
        { .longName = "--stop-on-failure", .description = "Stop on first test failure" },
        { .longName = "",
          .shortName = "-R",
          .description = "Include tests matching regex",
          .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-E",
          .description = "Exclude tests matching regex",
          .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-L",
          .description = "Include tests with matching label",
          .valueKind = OptionValueKind::String },
        { .longName = "--verbose", .shortName = "-V", .description = "Verbose output" },
        { .longName = "--timeout",
          .description = "Per-test timeout in seconds",
          .valueKind = OptionValueKind::String },
        { .longName = "--repeat",
          .description = "Repeat tests (e.g., until-fail:3)",
          .valueKind = OptionValueKind::String },
        { .longName = "--rerun-failed", .description = "Re-run only previously failed tests" },
    };

    return spec;
}

// ============================================================================
// CmakeQueryProvider implementation
// ============================================================================

std::vector<QueryResult> CmakeQueryProvider::query(std::string_view queryTag)
{
    if (queryTag != "presets")
        return {};

    // Phase 1: collect all presets (including hidden) from all files
    auto allPresets = std::unordered_map<std::string, PresetEntry> {};
    auto visited = std::set<std::string> {};
    collectPresetsFromFile("CMakePresets.json", ".", allPresets, visited);
    collectPresetsFromFile("CMakeUserPresets.json", ".", allPresets, visited);

    // Phase 2: filter to non-hidden, condition-satisfied presets
    auto results = std::vector<QueryResult> {};
    for (auto const& [name, entry]: allPresets)
    {
        if (entry.hidden)
            continue;

        auto checking = std::set<std::string> {};
        if (!isPresetAvailable(name, allPresets, checking))
            continue;

        results.push_back(QueryResult { .text = entry.name, .description = entry.displayName });
    }

    std::ranges::sort(results, {}, &QueryResult::text);
    return results;
}

} // namespace endo
