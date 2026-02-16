// SPDX-License-Identifier: Apache-2.0
#include "SshSpec.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace endo
{

namespace
{

    /// @brief Checks if a host pattern contains wildcards and should be skipped.
    /// @param pattern The host alias pattern to check.
    /// @return True if the pattern contains `*` or `?` (not useful as completion).
    [[nodiscard]] bool isWildcardPattern(std::string_view pattern) noexcept
    {
        return pattern.find('*') != std::string_view::npos || pattern.find('?') != std::string_view::npos;
    }

    /// @brief Resolves the user's home directory.
    [[nodiscard]] std::filesystem::path homeDirectory()
    {
        if (auto const* home = std::getenv("HOME"))
            return std::filesystem::path(home);
        return {};
    }

    /// @brief Resolves an Include path relative to ~/.ssh/ or as absolute.
    /// @param includePath The path from an Include directive.
    /// @param sshDir The ~/.ssh/ directory for relative resolution.
    /// @return Resolved absolute path.
    [[nodiscard]] std::filesystem::path resolveIncludePath(std::string_view includePath,
                                                           std::filesystem::path const& sshDir)
    {
        auto path = std::string(includePath);

        // Expand leading ~ to home directory
        if (path.starts_with("~/"))
            path = (homeDirectory() / path.substr(2)).string();

        auto const fsPath = std::filesystem::path(path);
        if (fsPath.is_absolute())
            return fsPath;

        // Relative paths are resolved against ~/.ssh/
        return sshDir / fsPath;
    }

    /// @brief Case-insensitive prefix comparison for SSH config directives.
    [[nodiscard]] bool directiveIs(std::string_view word, std::string_view directive) noexcept
    {
        if (word.size() != directive.size())
            return false;
        for (size_t i = 0; i < word.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(word[i]))
                != std::tolower(static_cast<unsigned char>(directive[i])))
                return false;
        }
        return true;
    }

} // namespace

CommandSpec createSshSpec()
{
    auto spec = CommandSpec {};
    spec.command = "ssh";
    spec.description = "OpenSSH remote login client";

    spec.globalOptions = {
        { .longName = "", .shortName = "-p", .description = "Port", .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-i",
          .description = "Identity file",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-l",
          .description = "Login name",
          .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-L",
          .description = "Local port forward",
          .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-R",
          .description = "Remote port forward",
          .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-D",
          .description = "Dynamic SOCKS proxy",
          .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-J",
          .description = "ProxyJump host",
          .valueKind = OptionValueKind::DynamicQuery,
          .queryTag = "hosts" },
        { .longName = "",
          .shortName = "-F",
          .description = "Config file",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-o",
          .description = "SSH option",
          .valueKind = OptionValueKind::String },
        { .longName = "", .shortName = "-N", .description = "No remote command" },
        { .longName = "", .shortName = "-T", .description = "Disable pseudo-terminal" },
        { .longName = "", .shortName = "-t", .description = "Force pseudo-terminal" },
        { .longName = "", .shortName = "-v", .description = "Verbose" },
        { .longName = "", .shortName = "-X", .description = "X11 forwarding" },
        { .longName = "", .shortName = "-A", .description = "Agent forwarding" },
        { .longName = "", .shortName = "-C", .description = "Compression" },
        { .longName = "", .shortName = "-q", .description = "Quiet" },
        { .longName = "", .shortName = "-4", .description = "IPv4 only" },
        { .longName = "", .shortName = "-6", .description = "IPv6 only" },
    };

    spec.positionalArgs = {
        { .kind = ArgKind::DynamicQuery, .description = "destination [user@]hostname", .queryTag = "hosts" },
    };

    return spec;
}

CommandSpec createScpSpec()
{
    auto spec = CommandSpec {};
    spec.command = "scp";
    spec.description = "OpenSSH secure file copy";

    spec.globalOptions = {
        { .longName = "", .shortName = "-P", .description = "Port", .valueKind = OptionValueKind::String },
        { .longName = "",
          .shortName = "-i",
          .description = "Identity file",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-F",
          .description = "Config file",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-o",
          .description = "SSH option",
          .valueKind = OptionValueKind::String },
        { .longName = "", .shortName = "-r", .description = "Recursive copy" },
        { .longName = "", .shortName = "-v", .description = "Verbose" },
        { .longName = "", .shortName = "-C", .description = "Compression" },
        { .longName = "", .shortName = "-q", .description = "Quiet" },
        { .longName = "", .shortName = "-4", .description = "IPv4 only" },
        { .longName = "", .shortName = "-6", .description = "IPv6 only" },
        { .longName = "", .shortName = "-3", .description = "Transfer via local host" },
    };

    spec.positionalArgs = {
        { .kind = ArgKind::DynamicQuery,
          .description = "source or destination [user@]host:path",
          .queryTag = "hosts",
          .repeatable = true },
    };

    return spec;
}

// ============================================================================
// SshQueryProvider implementation
// ============================================================================

void SshQueryProvider::parseConfigFile(std::filesystem::path const& configPath,
                                       std::vector<QueryResult>& results,
                                       std::set<std::string>& visited)
{
    auto ec = std::error_code {};
    auto const canonical = std::filesystem::canonical(configPath, ec);
    if (ec)
        return;
    if (!visited.insert(canonical.string()).second)
        return; // Already processed (cycle guard)

    auto file = std::ifstream(canonical);
    if (!file.is_open())
        return;

    auto const sshDir = canonical.parent_path();

    // Track current host aliases to attach HostName descriptions
    auto currentHosts = std::vector<size_t> {}; // Indices into results
    auto line = std::string {};

    while (std::getline(file, line))
    {
        // Strip leading whitespace
        auto const start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        auto const trimmed = std::string_view(line).substr(start);

        // Skip comments
        if (trimmed.starts_with('#'))
            continue;

        // Extract first word (directive)
        auto const spacePos = trimmed.find_first_of(" \t");
        if (spacePos == std::string::npos)
            continue;

        auto const directive = trimmed.substr(0, spacePos);
        auto const valueStart = trimmed.find_first_not_of(" \t", spacePos);
        if (valueStart == std::string::npos)
            continue;
        auto const value = trimmed.substr(valueStart);

        if (directiveIs(directive, "Host"))
        {
            currentHosts.clear();

            // Split by whitespace — each token is a separate host alias
            auto stream = std::istringstream(std::string(value));
            auto token = std::string {};
            while (stream >> token)
            {
                if (isWildcardPattern(token))
                    continue;
                currentHosts.push_back(results.size());
                results.push_back(QueryResult { .text = std::move(token) });
            }
        }
        else if (directiveIs(directive, "HostName"))
        {
            // Attach HostName as description to the current Host entries
            auto hostname = std::string(value);
            // Trim trailing whitespace
            while (!hostname.empty() && (hostname.back() == ' ' || hostname.back() == '\t'))
                hostname.pop_back();

            for (auto const idx: currentHosts)
                results[idx].description = hostname;
        }
        else if (directiveIs(directive, "Include"))
        {
            // Follow Include directives — may contain glob patterns
            auto includePath = std::string(value);
            // Trim trailing whitespace
            while (!includePath.empty() && (includePath.back() == ' ' || includePath.back() == '\t'))
                includePath.pop_back();

            auto const resolved = resolveIncludePath(includePath, sshDir);

            // Check if the path contains glob characters
            auto const pathStr = resolved.string();
            if (pathStr.find('*') != std::string::npos || pathStr.find('?') != std::string::npos)
            {
                // Expand glob patterns
                auto globEc = std::error_code {};
                auto const parentDir = resolved.parent_path();
                if (std::filesystem::exists(parentDir, globEc))
                {
                    for (auto const& entry: std::filesystem::directory_iterator(parentDir, globEc))
                    {
                        if (entry.is_regular_file())
                            parseConfigFile(entry.path(), results, visited);
                    }
                }
            }
            else
            {
                parseConfigFile(resolved, results, visited);
            }
        }
    }
}

std::vector<QueryResult> SshQueryProvider::query(std::string_view queryTag)
{
    if (queryTag != "hosts")
        return {};

    auto const home = homeDirectory();
    if (home.empty())
        return {};

    auto const configPath = home / ".ssh" / "config";

    auto results = std::vector<QueryResult> {};
    auto visited = std::set<std::string> {};
    parseConfigFile(configPath, results, visited);

    std::ranges::sort(results, {}, &QueryResult::text);
    return results;
}

} // namespace endo
