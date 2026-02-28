// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace endo::agent::local
{

/// A single downloadable part of a split GGUF model.
struct DownloadPart
{
    std::string url;          ///< Direct download URL for this part.
    std::string filename;     ///< Local filename for this part.
    size_t fileSizeBytes = 0; ///< Expected file size in bytes.
};

/// A downloadable quantization variant of a curated model.
struct ModelVariant
{
    std::string quantization; ///< Quantization method (e.g. "Q4_K_M", "Q5_K_M").
    std::string url;          ///< Direct download URL (HuggingFace). Primary/first part for split models.
    size_t fileSizeBytes = 0; ///< Total expected file size in bytes (sum for split models).
    size_t ramRequired = 0;   ///< Minimum RAM/VRAM in bytes.
    std::string filename;     ///< Local filename (first part for split models).
    std::vector<DownloadPart> parts; ///< Split-file parts. Empty for single-file models.
};

/// Returns all filenames for a variant (single or split).
/// For single-file models, returns {variant.filename}. For split models, returns the part filenames.
[[nodiscard]] auto allFilenames(ModelVariant const& variant) -> std::vector<std::string>;

/// A curated model in the registry.
struct CuratedModel
{
    std::string name;                   ///< Friendly name (e.g. "qwen2.5-coder-7b").
    std::string displayName;            ///< Display name (e.g. "Qwen 2.5 Coder 7B").
    std::string description;            ///< Short description.
    std::string architecture;           ///< Model architecture (e.g. "qwen2").
    size_t parameterCount = 0;          ///< Number of parameters.
    std::vector<ModelVariant> variants; ///< Available quantization variants.
    bool supportsToolUse = false;       ///< Whether the model supports tool calling.
    bool supportsVision = false;        ///< Whether the model supports image input.
};

/// Information about a locally available GGUF model file (or group of split files).
struct LocalModelInfo
{
    std::filesystem::path path; ///< Full path to the .gguf file (first part for split models).
    std::string filename;       ///< Filename without directory (first part for split models).
    size_t fileSizeBytes = 0;   ///< Total file size in bytes (sum for split models).
    std::vector<std::filesystem::path>
        splitPaths; ///< Paths to all split parts. Empty for single-file models.
};

/// Progress callback for model downloads.
/// @param totalBytes Total expected bytes (0 if unknown).
/// @param downloadedBytes Bytes downloaded so far.
/// @return true to continue, false to abort.
using DownloadProgressCallback = std::function<bool(size_t totalBytes, size_t downloadedBytes)>;

/// Returns the curated model catalog.
[[nodiscard]] auto curatedModels() -> std::span<CuratedModel const>;

/// Finds a curated model by name (case-insensitive substring match).
/// @param name The search query.
/// @return Pointer to the matching model, or nullptr if not found.
[[nodiscard]] auto findCuratedModel(std::string_view name) -> CuratedModel const*;

/// Returns the platform-appropriate model storage directory.
///
/// - Linux: ~/.local/share/endo/models/
/// - macOS: ~/Library/Application Support/endo/models/
/// - Windows: %LOCALAPPDATA%/endo/models/
[[nodiscard]] auto modelStorageDir() -> std::filesystem::path;

/// Discovers locally available GGUF model files in a directory.
/// @param dir Directory to scan.
/// @return List of discovered model files.
[[nodiscard]] auto discoverLocalModels(std::filesystem::path const& dir) -> std::vector<LocalModelInfo>;

/// Formats a byte count for human-readable display.
/// @param bytes Number of bytes.
/// @return Formatted string (e.g. "4.5 GB", "512 MB").
[[nodiscard]] auto formatBytes(size_t bytes) -> std::string;

} // namespace endo::agent::local
