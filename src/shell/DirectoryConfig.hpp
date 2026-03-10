// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

class Shell;

/// Callback type for diagnostic messages from directory config operations.
using DiagnosticSink = std::function<void(std::string const& message)>;

/// Returns a DiagnosticSink that writes to stderr (default behavior).
DiagnosticSink stderrDiagnosticSink();

/// Tracks which bindings a single directory config file introduced.
struct DirectoryConfigScope
{
    std::filesystem::path configDir;      ///< Directory containing .local-env.endo
    std::filesystem::path configFilePath; ///< Full path to .local-env.endo
    std::string contentHash;              ///< Hash of file content at load time
    std::vector<std::string> functions;   ///< Function names introduced
    std::vector<std::string> bindings;    ///< Value binding names introduced
    std::vector<std::string> properties;  ///< Property names introduced
    std::vector<std::string> envVars;     ///< Environment variable names introduced
};

/// Trust decision for a directory config file.
struct TrustEntry
{
    std::string contentHash; ///< Hash of the config file when trust was set
    bool allowed = false;    ///< Whether the config is trusted
};

/// Manages trust decisions for directory config files.
///
/// Trust decisions are persisted in ~/.config/endo/trusted-dirs.json.
/// Each entry maps a canonical config file path to its content hash and allowed status.
class DirectoryConfigTrustStore
{
  public:
    /// @param env Environment provider for resolving config home path.
    /// @param diag Diagnostic sink for error messages.
    explicit DirectoryConfigTrustStore(EnvironmentProvider& env,
                                       DiagnosticSink diag = stderrDiagnosticSink());

    /// Load trust decisions from persistent storage.
    void load();

    /// Save trust decisions to persistent storage.
    void save() const;

    /// Check if a config file is trusted with the given content hash.
    /// @return true if trusted, false if denied, nullopt if no decision recorded or hash changed.
    [[nodiscard]] std::optional<bool> checkTrust(std::filesystem::path const& configPath,
                                                 std::string const& contentHash) const;

    /// Record a trust decision for a config file.
    void setTrust(std::filesystem::path const& configPath, std::string const& contentHash, bool allowed);

    /// Revoke any trust decision for a config file path.
    void revokeTrust(std::filesystem::path const& configPath);

    /// Get all trust entries (canonical path -> TrustEntry).
    [[nodiscard]] auto const& entries() const noexcept { return _entries; }

  private:
    [[nodiscard]] std::filesystem::path trustFilePath() const;

    EnvironmentProvider& _env;
    DiagnosticSink _diag;
    std::unordered_map<std::string, TrustEntry> _entries; ///< canonical path -> trust entry
};

/// Manages directory-scoped configuration (.local-env.endo files).
///
/// When the working directory changes, discovers .local-env.endo files in
/// the directory ancestry (up to $HOME), loads trusted ones, and unloads
/// configs that no longer apply. Each loaded config's introduced bindings
/// are tracked so they can be cleanly removed on unload.
class DirectoryConfigManager
{
  public:
    /// @param shell Shell instance for executing config scripts and accessing persistent state.
    /// @param env Environment provider for directory and config home operations.
    /// @param diag Diagnostic sink for status and error messages (defaults to stderr).
    DirectoryConfigManager(Shell& shell,
                           EnvironmentProvider& env,
                           DiagnosticSink diag = stderrDiagnosticSink());

    /// Called after cd or at shell startup. Loads/unloads configs as needed.
    /// @param newCwd The new current working directory.
    void onDirectoryChanged(std::string const& newCwd);

    /// Trust and load a config file at the given path.
    /// @param configPath Path to the .local-env.endo file (or its directory).
    void allowConfig(std::filesystem::path const& configPath);

    /// Deny a config file at the given path.
    /// @param configPath Path to the .local-env.endo file (or its directory).
    void denyConfig(std::filesystem::path const& configPath);

    /// Revoke any trust decision for a config file path.
    /// @param configPath Path to the .local-env.endo file (or its directory).
    void revokeConfig(std::filesystem::path const& configPath);

    /// Get all trust entries for listing.
    [[nodiscard]] auto const& trustEntries() const noexcept { return _trustStore.entries(); }

    /// Get currently active scopes.
    [[nodiscard]] auto const& activeScopes() const noexcept { return _activeScopes; }

    /// Reload all active directory configs (unload then re-load).
    void reloadConfigs();

    /// Collected diagnostic messages (for testing).
    [[nodiscard]] std::vector<std::string> const& diagnostics() const noexcept { return _diagnostics; }

  private:
    /// Find .local-env.endo files from CWD upward to $HOME, returned root-first.
    [[nodiscard]] std::vector<std::filesystem::path> findConfigFiles(std::string const& cwd) const;

    /// Load a single directory config file (with trust check).
    /// @param configFile Path to the .local-env.endo file.
    void loadConfig(std::filesystem::path const& configFile);

    /// Unload a previously loaded directory config, removing its bindings.
    void unloadConfig(DirectoryConfigScope const& scope);

    /// Compute a content hash for change detection.
    [[nodiscard]] static std::string computeHash(std::string const& content);

    /// Resolve a path argument to a .local-env.endo file path.
    [[nodiscard]] static std::filesystem::path resolveConfigPath(std::filesystem::path const& path);

    /// Emit a diagnostic message via the configured sink.
    void diag(std::string const& message);

    Shell& _shell;
    EnvironmentProvider& _env;
    DirectoryConfigTrustStore _trustStore;
    DiagnosticSink _diag;
    std::vector<DirectoryConfigScope> _activeScopes;
    std::vector<std::string> _diagnostics; ///< Recorded diagnostics (always, regardless of sink)
};

} // namespace endo
