// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>
#include <map>

#include <agent/providers/local/ModelRegistry.hpp>

namespace endo::agent::local
{

namespace
{

    // clang-format off
    auto static const CuratedModelCatalog = std::array<CuratedModel, 6> {{
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
            .name = "deepseek-coder-v2-lite",
            .displayName = "DeepSeek Coder V2 Lite 16B",
            .description = "MoE coding model (2.4B active), 12 GB RAM",
            .architecture = "deepseek2",
            .parameterCount = 16'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf",
                    .fileSizeBytes = 10'360'000'000,
                    .ramRequired = 12'000'000'000,
                    .filename = "DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf",
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
                    .url = "https://huggingface.co/unsloth/Qwen3-Coder-30B-A3B-Instruct-GGUF/resolve/main/Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf",
                    .fileSizeBytes = 18'600'000'000,
                    .ramRequired = 24'000'000'000,
                    .filename = "qwen3-coder-30b-a3b-instruct-q4_k_m.gguf",
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
            .description = "Best coding (MoE, needs GPU), 145 GB RAM",
            .architecture = "qwen2_moe",
            .parameterCount = 235'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/unsloth/Qwen3-235B-A22B-GGUF/resolve/main/Q4_K_M/Qwen3-235B-A22B-Q4_K_M-00001-of-00003.gguf",
                    .fileSizeBytes = 142'154'000'000,
                    .ramRequired = 145'000'000'000,
                    .filename = "Qwen3-235B-A22B-Q4_K_M-00001-of-00003.gguf",
                    .parts = {{
                        {
                            .url = "https://huggingface.co/unsloth/Qwen3-235B-A22B-GGUF/resolve/main/Q4_K_M/Qwen3-235B-A22B-Q4_K_M-00001-of-00003.gguf",
                            .filename = "Qwen3-235B-A22B-Q4_K_M-00001-of-00003.gguf",
                            .fileSizeBytes = 49'945'000'000,
                        },
                        {
                            .url = "https://huggingface.co/unsloth/Qwen3-235B-A22B-GGUF/resolve/main/Q4_K_M/Qwen3-235B-A22B-Q4_K_M-00002-of-00003.gguf",
                            .filename = "Qwen3-235B-A22B-Q4_K_M-00002-of-00003.gguf",
                            .fileSizeBytes = 49'930'000'000,
                        },
                        {
                            .url = "https://huggingface.co/unsloth/Qwen3-235B-A22B-GGUF/resolve/main/Q4_K_M/Qwen3-235B-A22B-Q4_K_M-00003-of-00003.gguf",
                            .filename = "Qwen3-235B-A22B-Q4_K_M-00003-of-00003.gguf",
                            .fileSizeBytes = 42'279'000'000,
                        },
                    }},
                },
            }},
            .supportsToolUse = true,
            .supportsVision = false,
        },
        {
            .name = "deepseek-coder-v2-instruct",
            .displayName = "DeepSeek Coder V2 Instruct 236B",
            .description = "MoE 236B coding model (split download), 96 GB RAM",
            .architecture = "deepseek2",
            .parameterCount = 236'000'000'000,
            .variants = {{
                {
                    .quantization = "Q4_K_M",
                    .url = "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Instruct-Q4_K_M.gguf/DeepSeek-Coder-V2-Instruct-Q4_K_M-00001-of-00004.gguf",
                    .fileSizeBytes = 142'500'000'000,
                    .ramRequired = 96'000'000'000,
                    .filename = "DeepSeek-Coder-V2-Instruct-Q4_K_M-00001-of-00004.gguf",
                    .parts = {{
                        {
                            .url = "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Instruct-Q4_K_M.gguf/DeepSeek-Coder-V2-Instruct-Q4_K_M-00001-of-00004.gguf",
                            .filename = "DeepSeek-Coder-V2-Instruct-Q4_K_M-00001-of-00004.gguf",
                            .fileSizeBytes = 39'800'000'000,
                        },
                        {
                            .url = "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Instruct-Q4_K_M.gguf/DeepSeek-Coder-V2-Instruct-Q4_K_M-00002-of-00004.gguf",
                            .filename = "DeepSeek-Coder-V2-Instruct-Q4_K_M-00002-of-00004.gguf",
                            .fileSizeBytes = 40'000'000'000,
                        },
                        {
                            .url = "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Instruct-Q4_K_M.gguf/DeepSeek-Coder-V2-Instruct-Q4_K_M-00003-of-00004.gguf",
                            .filename = "DeepSeek-Coder-V2-Instruct-Q4_K_M-00003-of-00004.gguf",
                            .fileSizeBytes = 39'700'000'000,
                        },
                        {
                            .url = "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Instruct-Q4_K_M.gguf/DeepSeek-Coder-V2-Instruct-Q4_K_M-00004-of-00004.gguf",
                            .filename = "DeepSeek-Coder-V2-Instruct-Q4_K_M-00004-of-00004.gguf",
                            .fileSizeBytes = 23'000'000'000,
                        },
                    }},
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

auto allFilenames(ModelVariant const& variant) -> std::vector<std::string>
{
    if (variant.parts.empty())
        return { variant.filename };

    auto filenames = std::vector<std::string> {};
    filenames.reserve(variant.parts.size());
    for (auto const& part: variant.parts)
        filenames.push_back(part.filename);
    return filenames;
}

auto curatedModels() -> std::span<CuratedModel const>
{
    return CuratedModelCatalog;
}

auto findCuratedModel(std::string_view name) -> CuratedModel const*
{
    auto const it = std::ranges::find_if( // NOLINT(readability-qualified-auto)
        CuratedModelCatalog, [name](CuratedModel const& model) {
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

namespace
{
    /// Extracts the split-file group key from a filename, or empty if not a split file.
    /// Pattern: `*-NNNNN-of-NNNNN.gguf` → key is everything before `-NNNNN-of-NNNNN.gguf`.
    [[nodiscard]] auto splitGroupKey(std::string_view filename) -> std::string
    {
        // Must end with ".gguf"
        constexpr auto suffix = std::string_view { ".gguf" };
        if (filename.size() < suffix.size() || filename.substr(filename.size() - suffix.size()) != suffix)
            return {};

        // Strip ".gguf" to get stem
        auto const stem = filename.substr(0, filename.size() - suffix.size());

        // Look for "-NNNNN-of-NNNNN" at the end of stem
        // Minimum: "-N-of-N" = 7 chars, but we expect 5-digit parts: "-00001-of-00004" = 15 chars
        auto const ofPos = stem.rfind("-of-");
        if (ofPos == std::string_view::npos || ofPos == 0)
            return {};

        // Check that everything after "-of-" is digits
        auto const afterOf = stem.substr(ofPos + 4);
        if (afterOf.empty() || !std::ranges::all_of(afterOf, [](char ch) {
                return std::isdigit(static_cast<unsigned char>(ch));
            }))
            return {};

        // Find the dash before the part number
        auto const dashPos = stem.rfind('-', ofPos - 1);
        if (dashPos == std::string_view::npos || dashPos == 0)
            return {};

        // Check that between dashPos+1 and ofPos are all digits (the part number)
        auto const partNum = stem.substr(dashPos + 1, ofPos - dashPos - 1);
        if (partNum.empty() || !std::ranges::all_of(partNum, [](char ch) {
                return std::isdigit(static_cast<unsigned char>(ch));
            }))
            return {};

        return std::string(stem.substr(0, dashPos));
    }
} // namespace

auto discoverLocalModels(std::filesystem::path const& dir) -> std::vector<LocalModelInfo>
{
    auto models = std::vector<LocalModelInfo> {};

    auto ec = std::error_code {};
    if (!std::filesystem::is_directory(dir, ec))
        return models;

    // Collect all GGUF files, grouping split files by their base name.
    auto splitGroups = std::map<std::string, std::vector<std::pair<std::filesystem::path, size_t>>> {};

    for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;

        if (entry.path().extension() != ".gguf")
            continue;

        auto const fname = entry.path().filename().string();
        auto const fileSize = static_cast<size_t>(entry.file_size(ec));
        auto const key = splitGroupKey(fname);

        if (!key.empty())
        {
            splitGroups[key].emplace_back(entry.path(), fileSize);
        }
        else
        {
            models.push_back(LocalModelInfo {
                .path = entry.path(),
                .filename = fname,
                .fileSizeBytes = fileSize,
            });
        }
    }

    // Convert split groups into LocalModelInfo entries.
    for (auto& [key, parts]: splitGroups)
    {
        std::ranges::sort(parts, [](auto const& a, auto const& b) {
            return a.first.filename().string() < b.first.filename().string();
        });

        auto totalSize = size_t { 0 };
        auto splitPaths = std::vector<std::filesystem::path> {};
        splitPaths.reserve(parts.size());
        for (auto const& [path, size]: parts)
        {
            totalSize += size;
            splitPaths.push_back(path);
        }

        models.push_back(LocalModelInfo {
            .path = parts.front().first,
            .filename = parts.front().first.filename().string(),
            .fileSizeBytes = totalSize,
            .splitPaths = std::move(splitPaths),
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
