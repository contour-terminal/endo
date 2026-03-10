// SPDX-License-Identifier: Apache-2.0
#include <shell/DirectoryConfig.hpp>
#include <shell/Shell.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <print>
#include <ranges>
#include <set>
#include <string>

#include <nlohmann/json.hpp>
#include <platform/EnvironmentProvider.hpp>

namespace endo
{

namespace fs = std::filesystem;

DiagnosticSink stderrDiagnosticSink()
{
    return [](std::string const& message) {
        std::println(std::cerr, "{}", message);
    };
}

// ============================================================================
// DirectoryConfigTrustStore
// ============================================================================

DirectoryConfigTrustStore::DirectoryConfigTrustStore(EnvironmentProvider& env, DiagnosticSink diag):
    _env(env), _diag(std::move(diag))
{
}

fs::path DirectoryConfigTrustStore::trustFilePath() const
{
    auto const configDir = _env.configHome();
    if (!configDir)
        return {};
    return *configDir / "endo" / "trusted-dirs.json";
}

void DirectoryConfigTrustStore::load()
{
    auto const path = trustFilePath();
    if (path.empty() || !fs::exists(path))
        return;

    try
    {
        auto ifs = std::ifstream(path);
        auto const json = nlohmann::json::parse(ifs);
        for (auto const& [key, value]: json.items())
        {
            _entries[key] = TrustEntry {
                .contentHash = value.value("hash", ""),
                .allowed = value.value("allowed", false),
            };
        }
    }
    catch (std::exception const& e)
    {
        _diag(std::format("endo: warning: failed to load {}: {}", path.string(), e.what()));
    }
}

void DirectoryConfigTrustStore::save() const
{
    auto const path = trustFilePath();
    if (path.empty())
        return;

    try
    {
        fs::create_directories(path.parent_path());

        auto json = nlohmann::json::object();
        for (auto const& [key, entry]: _entries)
        {
            json[key] = nlohmann::json {
                { "hash", entry.contentHash },
                { "allowed", entry.allowed },
            };
        }

        auto ofs = std::ofstream(path);
        ofs << json.dump(2) << '\n';
    }
    catch (std::exception const& e)
    {
        _diag(std::format("endo: warning: failed to save {}: {}", path.string(), e.what()));
    }
}

std::optional<bool> DirectoryConfigTrustStore::checkTrust(fs::path const& configPath,
                                                          std::string const& contentHash) const
{
    auto const canonical = fs::weakly_canonical(configPath).string();
    auto const it = _entries.find(canonical);
    if (it == _entries.end())
        return std::nullopt;

    // Hash changed since trust was set — require re-approval
    if (it->second.contentHash != contentHash)
        return std::nullopt;

    return it->second.allowed;
}

void DirectoryConfigTrustStore::setTrust(fs::path const& configPath,
                                         std::string const& contentHash,
                                         bool allowed)
{
    auto const canonical = fs::weakly_canonical(configPath).string();
    _entries[canonical] = TrustEntry {
        .contentHash = contentHash,
        .allowed = allowed,
    };
    save();
}

void DirectoryConfigTrustStore::revokeTrust(fs::path const& configPath)
{
    auto const canonical = fs::weakly_canonical(configPath).string();
    _entries.erase(canonical);
    save();
}

// ============================================================================
// DirectoryConfigManager
// ============================================================================

DirectoryConfigManager::DirectoryConfigManager(Shell& shell, EnvironmentProvider& env, DiagnosticSink diag):
    _shell(shell), _env(env), _trustStore(env, diag), _diag(std::move(diag))
{
    _trustStore.load();
}

void DirectoryConfigManager::diag(std::string const& message)
{
    _diagnostics.push_back(message);
    if (_diag)
        _diag(message);
}

std::string DirectoryConfigManager::computeHash(std::string const& content)
{
    auto const hash = std::hash<std::string> {}(content);
    return std::format("{:016x}", hash);
}

fs::path DirectoryConfigManager::resolveConfigPath(fs::path const& path)
{
    if (fs::is_directory(path))
        return path / ".local-env.endo";
    return path;
}

std::vector<fs::path> DirectoryConfigManager::findConfigFiles(std::string const& cwd) const
{
    auto const home = _env.homeDirectory();
    if (!home)
        return {};

    auto const homeStr = home->string();
    auto dir = fs::path(cwd);

    // Collect config files from CWD upward, stopping at HOME
    std::vector<fs::path> configs;
    while (true)
    {
        auto const candidate = dir / ".local-env.endo";
        if (fs::exists(candidate))
            configs.push_back(candidate);

        // Stop at HOME — don't walk further up
        if (dir.string() == homeStr)
            break;

        auto const parent = dir.parent_path();
        if (parent == dir)
            break; // Reached filesystem root
        dir = parent;
    }

    // Reverse to get root-first order (outer configs loaded before inner)
    std::ranges::reverse(configs);
    return configs;
}

void DirectoryConfigManager::loadConfig(fs::path const& configFile)
{
    // Read file content
    std::string content;
    try
    {
        auto ifs = std::ifstream(configFile);
        if (!ifs)
        {
            diag(std::format("endo: warning: cannot read {}", configFile.string()));
            return;
        }
        content = std::string(std::istreambuf_iterator<char>(ifs), {});
    }
    catch (std::exception const& e)
    {
        diag(std::format("endo: warning: error reading {}: {}", configFile.string(), e.what()));
        return;
    }

    auto const hash = computeHash(content);

    // Check trust
    auto const trusted = _trustStore.checkTrust(configFile, hash);
    if (!trusted.has_value())
    {
        diag(std::format("endo: directory config found but not trusted: {}", configFile.string()));
        diag(std::format("endo: run 'dirconfig allow {}' to trust it", configFile.parent_path().string()));
        return;
    }
    if (!*trusted)
        return;

    // Snapshot state before execution
    auto& state = _shell.fsharpState();
    auto const functionsBefore = [&] {
        std::set<std::string> names;
        for (auto const& [name, _]: state.functions)
            names.insert(name);
        return names;
    }();
    auto const bindingsBefore = [&] {
        std::set<std::string> names;
        for (auto const& vb: state.valueBindings)
            names.insert(vb.name);
        return names;
    }();
    auto const propertiesBefore = [&] {
        std::set<std::string> names;
        for (auto const& [name, _]: state.properties)
            names.insert(name);
        return names;
    }();
    auto const envKeysBefore = [&] {
        auto keys = _env.keys();
        return std::set<std::string>(keys.begin(), keys.end());
    }();

    // Execute the config script
    auto const result = _shell.executeConfigScript(content, configFile.string());
    if (result != 0)
        diag(std::format("endo: warning: {} exited with code {}", configFile.string(), result));

    // Diff state to find new entries
    auto scope = DirectoryConfigScope {
        .configDir = configFile.parent_path(),
        .configFilePath = configFile,
        .contentHash = hash,
    };

    for (auto const& [name, _]: state.functions)
    {
        if (!functionsBefore.contains(name))
            scope.functions.push_back(name);
    }
    for (auto const& vb: state.valueBindings)
    {
        if (!bindingsBefore.contains(vb.name))
            scope.bindings.push_back(vb.name);
    }
    for (auto const& [name, _]: state.properties)
    {
        if (!propertiesBefore.contains(name))
            scope.properties.push_back(name);
    }
    auto const envKeysAfter = [&] {
        auto keys = _env.keys();
        return std::set<std::string>(keys.begin(), keys.end());
    }();
    for (auto const& key: envKeysAfter)
    {
        if (!envKeysBefore.contains(key))
            scope.envVars.push_back(key);
    }

    _activeScopes.push_back(std::move(scope));
}

void DirectoryConfigManager::unloadConfig(DirectoryConfigScope const& scope)
{
    auto& state = _shell.fsharpState();

    // Remove functions
    for (auto const& name: scope.functions)
        state.functions.erase(name);

    // Remove value bindings (by name, preserving order of remaining)
    auto const bindingNames = std::set<std::string>(scope.bindings.begin(), scope.bindings.end());
    std::erase_if(state.valueBindings, [&](auto const& vb) { return bindingNames.contains(vb.name); });

    // Remove mutable snapshots for unloaded bindings
    for (auto const& name: scope.bindings)
        state.mutableSnapshots.erase(name);

    // Remove properties
    for (auto const& name: scope.properties)
        state.properties.erase(name);

    // Unset environment variables
    for (auto const& name: scope.envVars)
        _env.unset(name);
}

void DirectoryConfigManager::onDirectoryChanged(std::string const& newCwd)
{
    auto const newConfigs = findConfigFiles(newCwd);

    // Build list of config file paths from current active scopes
    auto const oldConfigs = [&] {
        std::vector<fs::path> paths;
        paths.reserve(_activeScopes.size());
        for (auto const& scope: _activeScopes)
            paths.push_back(scope.configFilePath);
        return paths;
    }();

    // Find common prefix length
    auto const commonLen = [&] {
        auto const maxLen = std::min(oldConfigs.size(), newConfigs.size());
        size_t i = 0;
        while (i < maxLen && fs::weakly_canonical(oldConfigs[i]) == fs::weakly_canonical(newConfigs[i]))
            ++i;
        return i;
    }();

    // Unload stale configs (inner-first, i.e., reverse order)
    for (auto i = _activeScopes.size(); i > commonLen; --i)
        unloadConfig(_activeScopes[i - 1]);
    _activeScopes.resize(commonLen);

    // Load new configs (root-first, i.e., forward order from commonLen)
    for (auto i = commonLen; i < newConfigs.size(); ++i)
        loadConfig(newConfigs[i]);
}

void DirectoryConfigManager::allowConfig(fs::path const& configPath)
{
    auto const resolved = resolveConfigPath(configPath);
    if (!fs::exists(resolved))
    {
        diag(std::format("endo: config file not found: {}", resolved.string()));
        return;
    }

    std::string content;
    try
    {
        auto ifs = std::ifstream(resolved);
        content = std::string(std::istreambuf_iterator<char>(ifs), {});
    }
    catch (std::exception const& e)
    {
        diag(std::format("endo: error reading {}: {}", resolved.string(), e.what()));
        return;
    }

    auto const hash = computeHash(content);
    _trustStore.setTrust(resolved, hash, true);
    diag(std::format("endo: trusted {}", resolved.string()));

    // Trigger reload to pick up newly trusted config
    onDirectoryChanged(_env.currentDirectory());
}

void DirectoryConfigManager::denyConfig(fs::path const& configPath)
{
    auto const resolved = resolveConfigPath(configPath);
    if (!fs::exists(resolved))
    {
        diag(std::format("endo: config file not found: {}", resolved.string()));
        return;
    }

    std::string content;
    try
    {
        auto ifs = std::ifstream(resolved);
        content = std::string(std::istreambuf_iterator<char>(ifs), {});
    }
    catch (std::exception const& e)
    {
        diag(std::format("endo: error reading {}: {}", resolved.string(), e.what()));
        return;
    }

    auto const hash = computeHash(content);
    _trustStore.setTrust(resolved, hash, false);
    diag(std::format("endo: denied {}", resolved.string()));

    // Unload if currently active
    auto const canonical = fs::weakly_canonical(resolved);
    for (auto it = _activeScopes.begin(); it != _activeScopes.end(); ++it)
    {
        if (fs::weakly_canonical(it->configFilePath) == canonical)
        {
            unloadConfig(*it);
            _activeScopes.erase(it);
            break;
        }
    }
}

void DirectoryConfigManager::revokeConfig(fs::path const& configPath)
{
    auto const resolved = resolveConfigPath(configPath);
    _trustStore.revokeTrust(resolved);
    diag(std::format("endo: revoked trust for {}", resolved.string()));

    // Unload if currently active
    auto const canonical = fs::weakly_canonical(resolved);
    for (auto it = _activeScopes.begin(); it != _activeScopes.end(); ++it)
    {
        if (fs::weakly_canonical(it->configFilePath) == canonical)
        {
            unloadConfig(*it);
            _activeScopes.erase(it);
            break;
        }
    }
}

void DirectoryConfigManager::reloadConfigs()
{
    auto const cwd = _env.currentDirectory();

    // Unload all active configs (inner-first)
    for (auto i = _activeScopes.size(); i > 0; --i)
        unloadConfig(_activeScopes[i - 1]);
    _activeScopes.clear();

    // Reload trust store and re-discover configs
    _trustStore.load();
    onDirectoryChanged(cwd);
}

} // namespace endo
