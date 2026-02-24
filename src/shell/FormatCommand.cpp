// SPDX-License-Identifier: Apache-2.0
#include "FormatCommand.hpp"

#include <endo-language/format/FormatConfig.hpp>
#include <endo-language/format/SourceFormatter.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <sstream>
#include <string>

namespace endo::format
{

namespace
{

    struct FormatOptions
    {
        bool check = false;
        bool diff = false;
        bool toStdout = false;
        std::string configPath;
        std::vector<std::string_view> files;
    };

    FormatOptions parseFormatArgs(std::span<char const* const> args)
    {
        FormatOptions opts;
        for (size_t i = 0; i < args.size(); ++i)
        {
            auto const arg = std::string_view(args[i]);
            if (arg == "--check")
                opts.check = true;
            else if (arg == "--diff")
                opts.diff = true;
            else if (arg == "--stdout")
                opts.toStdout = true;
            else if (arg == "--config" && i + 1 < args.size())
                opts.configPath = args[++i];
            else if (arg.starts_with("--config="))
                opts.configPath = std::string(arg.substr(9));
            else if (arg == "-h" || arg == "--help")
            {
                std::print(R"(Usage: endo format [OPTIONS] FILE...

Options:
  --check         Check formatting without modifying files (exit 1 if unformatted)
  --diff          Show diff between original and formatted output
  --stdout        Print formatted output to stdout instead of writing in-place
  --config PATH   Use a specific .endo-format config file
  -h, --help      Show this help message

If no files are specified, reads from stdin.
)");
                std::exit(EXIT_SUCCESS);
            }
            else if (!arg.starts_with("-"))
                opts.files.push_back(arg);
            else
            {
                std::print(stderr, "endo format: unknown option: {}\n", arg);
                std::exit(EXIT_FAILURE);
            }
        }
        return opts;
    }

    std::string readFile(std::filesystem::path const& path)
    {
        std::ifstream file(path);
        if (!file)
            return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    bool writeFile(std::filesystem::path const& path, std::string const& content)
    {
        std::ofstream file(path);
        if (!file)
            return false;
        file << content;
        return true;
    }

} // namespace

int runFormatCommand(std::span<char const* const> args)
{
    auto const opts = parseFormatArgs(args);

    // Load config
    FormatConfig config;
    if (!opts.configPath.empty())
    {
        auto loaded = FormatConfig::load(opts.configPath);
        if (!loaded)
        {
            std::print(stderr, "endo format: {}\n", loaded.error());
            return EXIT_FAILURE;
        }
        config = *loaded;
    }
    else if (!opts.files.empty())
    {
        auto const dir = std::filesystem::absolute(std::filesystem::path(opts.files[0])).parent_path();
        if (auto found = FormatConfig::findConfigFile(dir))
            config = FormatConfig::load(*found).value_or(FormatConfig {});
    }

    if (opts.files.empty())
    {
        // Read from stdin
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        auto const source = ss.str();
        auto const formatted = SourceFormatter::format(source, config);

        if (opts.check)
            return (formatted == source) ? EXIT_SUCCESS : EXIT_FAILURE;

        std::print("{}", formatted);
        return EXIT_SUCCESS;
    }

    auto anyChanged = false;
    for (auto const& filePath: opts.files)
    {
        auto const path = std::filesystem::path(filePath);
        auto const source = readFile(path);
        if (source.empty() && !std::filesystem::exists(path))
        {
            std::print(stderr, "endo format: cannot read file: {}\n", filePath);
            return EXIT_FAILURE;
        }

        auto const formatted = SourceFormatter::format(source, config);

        if (opts.check)
        {
            if (formatted != source)
            {
                std::print(stderr, "{}: would be reformatted\n", filePath);
                anyChanged = true;
            }
            continue;
        }

        if (opts.diff)
        {
            if (formatted != source)
            {
                std::print("--- {}\n+++ {}\n", filePath, filePath);
                // Simple line-by-line diff output
                std::print("{}", formatted);
            }
            continue;
        }

        if (opts.toStdout)
        {
            std::print("{}", formatted);
            continue;
        }

        // In-place formatting
        if (formatted != source)
        {
            if (!writeFile(path, formatted))
            {
                std::print(stderr, "endo format: cannot write file: {}\n", filePath);
                return EXIT_FAILURE;
            }
            std::print(stderr, "formatted: {}\n", filePath);
        }
    }

    return (opts.check && anyChanged) ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace endo::format
