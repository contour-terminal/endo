// SPDX-License-Identifier: Apache-2.0
#include "ModelsCommand.hpp"

#include <agent/providers/local/ModelRegistry.hpp>
#include <http/HttpClient.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #include <io.h>
#else
    #include <unistd.h>
#endif

using namespace std::string_view_literals;

namespace endo::agent::local
{

namespace
{
    /// Returns true if stdout is a terminal (for color output).
    [[nodiscard]] auto isTerminal() -> bool
    {
#if defined(_WIN32)
        return _isatty(_fileno(stdout));
#else
        return isatty(STDOUT_FILENO) != 0;
#endif
    }

    /// ANSI color helpers.
    struct Colors
    {
        std::string_view reset;
        std::string_view bold;
        std::string_view dim;
        std::string_view green;
        std::string_view red;
        std::string_view yellow;
        std::string_view cyan;
    };

    [[nodiscard]] auto getColors() -> Colors
    {
        if (!isTerminal())
            return {};
        return {
            .reset = "\033[0m",
            .bold = "\033[1m",
            .dim = "\033[2m",
            .green = "\033[32m",
            .red = "\033[31m",
            .yellow = "\033[33m",
            .cyan = "\033[36m",
        };
    }

    /// Checks if a curated model variant is downloaded locally.
    [[nodiscard]] auto isModelDownloaded(ModelVariant const& variant) -> bool
    {
        auto const dir = modelStorageDir();
        return std::filesystem::exists(dir / variant.filename);
    }

    /// Runs `endo agent models list`.
    auto runList() -> int
    {
        auto const c = getColors();
        auto const models = curatedModels();
        auto const localModels = discoverLocalModels(modelStorageDir());

        std::print("\n{}Available Models:{}\n\n", c.bold, c.reset);
        std::print("  {}{:<24}{:<10}{:<10}{:<16}{}{}\n",
                   c.bold,
                   "Name",
                   "Size",
                   "RAM",
                   "Status",
                   "Description",
                   c.reset);
        std::print("  {}{:─<24}{:─<10}{:─<10}{:─<16}{:─<30}{}\n",
                   c.dim,
                   "",
                   "",
                   "",
                   "",
                   "",
                   c.reset);

        for (auto const& model: models)
        {
            if (model.variants.empty())
                continue;

            auto const& variant = model.variants.front();
            auto const downloaded = isModelDownloaded(variant);
            auto const statusColor = downloaded ? c.green : c.dim;
            auto const statusText = downloaded ? "downloaded"sv : "not installed"sv;

            std::print("  {:<24}{:<10}{:<10}{}{:<16}{}{}\n",
                       model.name,
                       formatBytes(variant.fileSizeBytes),
                       formatBytes(variant.ramRequired),
                       statusColor,
                       statusText,
                       c.reset,
                       model.description);
        }

        // Show any additional local models not in the curated list.
        for (auto const& local: localModels)
        {
            auto const isCurated = std::ranges::any_of(models, [&](auto const& m) {
                return std::ranges::any_of(
                    m.variants, [&](auto const& v) { return v.filename == local.filename; });
            });
            if (!isCurated)
            {
                std::print("  {:<24}{:<10}{:<10}{}{:<16}{}{}\n",
                           local.filename,
                           formatBytes(local.fileSizeBytes),
                           "",
                           c.green,
                           "downloaded",
                           c.reset,
                           "(custom model)");
            }
        }

        std::print(
            "\n{}Use: endo agent models download <name> [--quant Q4_K_M]{}\n\n", c.dim, c.reset);
        return EXIT_SUCCESS;
    }

    /// Renders a terminal progress bar.
    void renderProgress(Colors const& c,
                        std::string_view label,
                        size_t totalBytes,
                        size_t downloadedBytes)
    {
        constexpr int barWidth = 30;
        auto const fraction =
            (totalBytes > 0) ? static_cast<double>(downloadedBytes) / static_cast<double>(totalBytes) : 0.0;
        auto const filled = static_cast<int>(fraction * barWidth);

        std::print("\r  {}[", c.cyan);
        for (int i = 0; i < barWidth; ++i)
            std::print("{}", (i < filled) ? "█" : "░");
        std::print("]{} {:3.0f}%  {} / {}   ",
                   c.reset,
                   fraction * 100.0,
                   formatBytes(downloadedBytes),
                   (totalBytes > 0) ? formatBytes(totalBytes) : "?");
        std::fflush(stdout);
    }

    /// Runs `endo agent models download <name> [--quant <quant>]`.
    auto runDownload(std::span<char const* const> args) -> int
    {
        if (args.empty())
        {
            std::print(stderr, "Usage: endo agent models download <name> [--quant Q4_K_M]\n");
            return EXIT_FAILURE;
        }

        auto const name = std::string_view(args[0]);
        auto quantHint = "Q4_K_M"sv;

        // Parse --quant option.
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (std::string_view(args[i]) == "--quant" && i + 1 < args.size())
            {
                quantHint = std::string_view(args[++i]);
            }
        }

        auto const* model = findCuratedModel(name);
        if (!model)
        {
            std::print(stderr, "Unknown model: {}\n", name);
            std::print(stderr, "Use 'endo agent models list' to see available models.\n");
            return EXIT_FAILURE;
        }

        // Find the requested quantization variant.
        auto const* variant = static_cast<ModelVariant const*>(nullptr);
        for (auto const& v: model->variants)
        {
            if (v.quantization == quantHint)
            {
                variant = &v;
                break;
            }
        }

        if (!variant)
        {
            // Fall back to first variant.
            if (model->variants.empty())
            {
                std::print(stderr, "No variants available for model: {}\n", model->name);
                return EXIT_FAILURE;
            }
            variant = &model->variants.front();
        }

        // Check if already downloaded.
        auto const dir = modelStorageDir();
        auto const destPath = dir / variant->filename;
        if (std::filesystem::exists(destPath))
        {
            auto const c = getColors();
            std::print(
                "{}Model already downloaded:{} {}\n", c.green, c.reset, destPath.string());
            return EXIT_SUCCESS;
        }

        // Create model directory.
        std::filesystem::create_directories(dir);

        auto const c = getColors();
        std::print("Downloading {} ({}, {})...\n",
                   model->displayName,
                   variant->quantization,
                   formatBytes(variant->fileSizeBytes));
        std::print("{}URL: {}{}\n", c.dim, variant->url, c.reset);
        std::print("{}Destination: {}{}\n\n", c.dim, destPath.string(), c.reset);

        // Download using HttpClient.
        auto httpClient = http::HttpClient {};
        auto request = http::HttpRequest {
            .url = variant->url,
            .method = http::HttpMethod::Get,
            .timeout = std::nullopt, // No timeout for large downloads.
            .maxResponseSize = 0,    // Ignored by download().
            .progressCallback =
                [&c](size_t total, size_t now) -> bool {
                    renderProgress(c, "Downloading", total, now);
                    return true; // Continue downloading.
                },
            .followRedirects = true,
        };

        auto const result = httpClient.download(request, destPath);

        if (!result.has_value())
        {
            std::print("\n{}Download failed:{} {}\n", c.red, c.reset, result.error().message);
            // Clean up partial file.
            std::error_code ec;
            std::filesystem::remove(destPath, ec);
            return EXIT_FAILURE;
        }

        if (result->statusCode != 200)
        {
            std::print(
                "\n{}Download failed:{} HTTP {}\n", c.red, c.reset, result->statusCode);
            std::error_code ec;
            std::filesystem::remove(destPath, ec);
            return EXIT_FAILURE;
        }

        std::print("\n\n{}Downloaded:{} {}\n\n", c.green, c.reset, destPath.string());
        std::print("To use this model, add to ~/.config/endo/init.endo:\n");
        std::print("  agent_local_model_path <- \"{}\"\n", destPath.string());
        std::print("  agent_provider <- \"local\"\n\n");

        return EXIT_SUCCESS;
    }

    /// Runs `endo agent models remove <name>`.
    auto runRemove(std::span<char const* const> args) -> int
    {
        if (args.empty())
        {
            std::print(stderr, "Usage: endo agent models remove <name>\n");
            return EXIT_FAILURE;
        }

        auto const name = std::string_view(args[0]);
        auto const dir = modelStorageDir();
        auto const localModels = discoverLocalModels(dir);

        // Find matching file.
        for (auto const& local: localModels)
        {
            if (local.filename.find(name) != std::string::npos)
            {
                std::error_code ec;
                std::filesystem::remove(local.path, ec);
                if (ec)
                {
                    std::print(stderr, "Failed to remove {}: {}\n", local.path.string(), ec.message());
                    return EXIT_FAILURE;
                }
                auto const c = getColors();
                std::print("{}Removed:{} {}\n", c.green, c.reset, local.path.string());
                return EXIT_SUCCESS;
            }
        }

        std::print(stderr, "No downloaded model matching '{}' found.\n", name);
        return EXIT_FAILURE;
    }

    /// Runs `endo agent models info <name>`.
    auto runInfo(std::span<char const* const> args) -> int
    {
        if (args.empty())
        {
            std::print(stderr, "Usage: endo agent models info <name>\n");
            return EXIT_FAILURE;
        }

        auto const name = std::string_view(args[0]);
        auto const* model = findCuratedModel(name);
        if (!model)
        {
            std::print(stderr, "Unknown model: {}\n", name);
            return EXIT_FAILURE;
        }

        auto const c = getColors();
        std::print("\n{}{}{}\n\n", c.bold, model->displayName, c.reset);
        std::print("  {:<18}{}\n", "Architecture:", model->architecture);
        std::print("  {:<18}{}B\n",
                   "Parameters:",
                   model->parameterCount / 1'000'000'000);
        std::print("  {:<18}{}\n",
                   "Tool Use:",
                   model->supportsToolUse ? "Yes" : "No");
        std::print("  {:<18}{}\n",
                   "Vision:",
                   model->supportsVision ? "Yes" : "No");

        if (!model->variants.empty())
        {
            std::print("\n  {}Available quantizations:{}\n", c.bold, c.reset);
            for (auto const& v: model->variants)
            {
                auto const downloaded = isModelDownloaded(v);
                auto const marker = downloaded ? std::format("{}(downloaded){}", c.green, c.reset) : "";
                std::print("    {:<10}{:<10}{:<12}{}\n",
                           v.quantization,
                           formatBytes(v.fileSizeBytes),
                           std::format("{} RAM", formatBytes(v.ramRequired)),
                           marker);
            }
        }

        std::print("\n");
        return EXIT_SUCCESS;
    }

    void printUsage()
    {
        std::print(stderr, "Usage: endo agent models <subcommand>\n\n");
        std::print(stderr, "Subcommands:\n");
        std::print(stderr, "  list                    Show available and downloaded models\n");
        std::print(stderr, "  download <name> [opts]  Download a curated model\n");
        std::print(stderr, "  remove <name>           Delete a downloaded model\n");
        std::print(stderr, "  info <name>             Show detailed model information\n");
    }
} // namespace

auto runModelsCommand(std::span<char const* const> args) -> int
{
    if (args.empty())
    {
        printUsage();
        return EXIT_FAILURE;
    }

    auto const subcommand = std::string_view(args[0]);

    if (subcommand == "list")
        return runList();
    if (subcommand == "download")
        return runDownload(args.subspan(1));
    if (subcommand == "remove")
        return runRemove(args.subspan(1));
    if (subcommand == "info")
        return runInfo(args.subspan(1));

    std::print(stderr, "Unknown models subcommand: {}\n", subcommand);
    printUsage();
    return EXIT_FAILURE;
}

} // namespace endo::agent::local
