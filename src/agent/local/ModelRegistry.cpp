// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>

#include <agent/local/ModelRegistry.hpp>

namespace endo::agent::local
{

namespace
{

    // clang-format off
    auto static const CuratedModelCatalog = std::array<CuratedModel, 4> {{
        {
            .name = "qwen2.5-coder-7b",
            .displayName = "Qwen 2.5 Coder 7B",
            .description = "Fast coding model, 8 GB RAM",
            .architecture = "qwen2",
            .parameterCount = 7'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
                    .fileSizeBytes = 4'700'000'000,
                    .ramRequired = 8'000'000'000,
                    .filename = "qwen2.5-coder-7b-instruct-q4_k_m.gguf",
                },
            }},
            .supportsToolUse = true,
            .supportsVision = false,
        },
        {
            .name = "qwen3-coder-30b",
            .displayName = "Qwen 3 Coder 30B",
            .description = "Balanced coding agent, 24 GB RAM",
            .architecture = "qwen2",
            .parameterCount = 30'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/unsloth/Qwen3-Coder-30B-A3B-GGUF/resolve/main/Qwen3-Coder-30B-A3B-Q4_K_M.gguf",
                    .fileSizeBytes = 17'000'000'000,
                    .ramRequired = 24'000'000'000,
                    .filename = "qwen3-coder-30b-a3b-q4_k_m.gguf",
                },
            }},
            .supportsToolUse = true,
            .supportsVision = false,
        },
        {
            .name = "llama3.3-70b",
            .displayName = "Llama 3.3 70B",
            .description = "Strong general + coding, 48 GB RAM",
            .architecture = "llama",
            .parameterCount = 70'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/bartowski/Llama-3.3-70B-Instruct-GGUF/resolve/main/Llama-3.3-70B-Instruct-Q4_K_M.gguf",
                    .fileSizeBytes = 40'000'000'000,
                    .ramRequired = 48'000'000'000,
                    .filename = "llama-3.3-70b-instruct-q4_k_m.gguf",
                },
            }},
            .supportsToolUse = true,
            .supportsVision = false,
        },
        {
            .name = "qwen3-235b-moe",
            .displayName = "Qwen 3 235B MoE",
            .description = "Best coding (MoE, needs GPU), 52 GB RAM",
            .architecture = "qwen2_moe",
            .parameterCount = 235'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/unsloth/Qwen3-235B-A22B-GGUF/resolve/main/Qwen3-235B-A22B-Q4_K_M.gguf",
                    .fileSizeBytes = 48'000'000'000,
                    .ramRequired = 52'000'000'000,
                    .filename = "qwen3-235b-a22b-q4_k_m.gguf",
                },
            }},
            .supportsToolUse = true,
            .supportsVision = false,
        },
    }};
    // clang-format on

    /// Case-insensitive substring search.
    [[nodiscard]] auto containsCaseInsensitive(std::string_view haystack, std::string_view needle) -> bool
    {
        if (needle.size() > haystack.size())
            return false;

        return std::ranges::search(haystack,
                                   needle,
                                   [](char a, char b) {
                                       return std::tolower(static_cast<unsigned char>(a))
                                              == std::tolower(static_cast<unsigned char>(b));
                                   })
                   .begin()
               != haystack.end();
    }

} // namespace

auto curatedModels() -> std::span<CuratedModel const>
{
    return CuratedModelCatalog;
}

auto findCuratedModel(std::string_view name) -> CuratedModel const*
{
    auto const it = std::ranges::find_if(CuratedModelCatalog, [name](CuratedModel const& model) {
        return containsCaseInsensitive(model.name, name);
    });

    if (it != CuratedModelCatalog.end())
        return &(*it);

    return nullptr;
}

auto modelStorageDir() -> std::filesystem::path
{
#if defined(__APPLE__)
    if (auto const* home = std::getenv("HOME"))
        return std::filesystem::path(home) / "Library" / "Application Support" / "endo" / "models";
    return std::filesystem::path("/tmp/endo/models");
#elif defined(_WIN32)
    if (auto const* localAppData = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(localAppData) / "endo" / "models";
    return std::filesystem::path("C:/endo/models");
#else
    // Linux / other Unix: XDG_DATA_HOME or ~/.local/share
    if (auto const* xdgData = std::getenv("XDG_DATA_HOME"); xdgData && *xdgData != '\0')
        return std::filesystem::path(xdgData) / "endo" / "models";
    if (auto const* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local" / "share" / "endo" / "models";
    return std::filesystem::path("/tmp/endo/models");
#endif
}

auto discoverLocalModels(std::filesystem::path const& dir) -> std::vector<LocalModelInfo>
{
    auto models = std::vector<LocalModelInfo> {};

    auto ec = std::error_code {};
    if (!std::filesystem::is_directory(dir, ec))
        return models;

    for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;

        if (entry.path().extension() != ".gguf")
            continue;

        models.push_back(LocalModelInfo {
            .path = entry.path(),
            .filename = entry.path().filename().string(),
            .fileSizeBytes = static_cast<size_t>(entry.file_size(ec)),
        });
    }

    std::ranges::sort(
        models, [](LocalModelInfo const& a, LocalModelInfo const& b) { return a.filename < b.filename; });

    return models;
}

auto formatBytes(size_t bytes) -> std::string
{
    constexpr auto KB = size_t { 1024 };
    constexpr auto MB = size_t { 1024 * 1024 };
    constexpr auto GB = size_t { 1024 * 1024 * 1024 };

    if (bytes >= GB)
        return std::format("{:.1f} GB", static_cast<double>(bytes) / static_cast<double>(GB));
    if (bytes >= MB)
        return std::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(MB));
    if (bytes >= KB)
        return std::format("{:.1f} KB", static_cast<double>(bytes) / static_cast<double>(KB));
    return std::format("{} bytes", bytes);
}

} // namespace endo::agent::local
