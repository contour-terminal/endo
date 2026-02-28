// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM

    #include <agent/local/ChatTemplate.hpp>

    #include <cstdint>
    #include <expected>
    #include <filesystem>
    #include <optional>
    #include <string>
    #include <vector>

// Forward declarations for llama.cpp types.
struct llama_model;

namespace endo::agent::local
{

/// Information about a loaded GGUF model.
struct LoadedModelInfo
{
    std::filesystem::path path;       ///< Full path to the GGUF file.
    std::string name;                 ///< Model name from GGUF metadata.
    std::string architecture;         ///< Architecture (e.g. "llama", "qwen2").
    size_t parameterCount = 0;        ///< Number of parameters.
    size_t fileSizeBytes = 0;         ///< File size in bytes.
    size_t contextLength = 0;         ///< Maximum context length from metadata.
    ChatTemplateFormat chatTemplate = ChatTemplateFormat::Generic; ///< Detected chat template.
    bool supportsToolUse = false;     ///< Whether the model supports tool calling.
    bool supportsVision = false;      ///< Whether the model has a vision encoder.
};

/// Manages GGUF model discovery, loading, and lifecycle.
///
/// Owns the llama_model pointer and provides shared access for multiple
/// llama_context instances (model weights are thread-safe for reads).
class ModelManager
{
  public:
    /// Constructs a ModelManager.
    /// @param modelDir Directory to scan for GGUF models.
    /// @param gpuLayers Number of layers to offload to GPU (-1 = all, 0 = CPU only).
    /// @param flashAttention Whether to enable flash attention.
    explicit ModelManager(std::filesystem::path modelDir, int32_t gpuLayers = -1, bool flashAttention = true);

    ~ModelManager();

    ModelManager(ModelManager const&) = delete;
    auto operator=(ModelManager const&) -> ModelManager& = delete;
    ModelManager(ModelManager&&) noexcept;
    auto operator=(ModelManager&&) noexcept -> ModelManager&;

    /// Loads a model from the given GGUF file.
    /// @param path Full path to the GGUF file.
    /// @return Error message on failure, or std::nullopt on success.
    [[nodiscard]] auto loadModel(std::filesystem::path const& path) -> std::optional<std::string>;

    /// Unloads the currently loaded model, freeing memory.
    void unloadModel();

    /// Returns the raw llama_model pointer (for context creation).
    /// @return The loaded model, or nullptr if no model is loaded.
    [[nodiscard]] auto model() const noexcept -> llama_model*;

    /// Returns whether a model is currently loaded.
    [[nodiscard]] auto isLoaded() const noexcept -> bool;

    /// Returns information about the currently loaded model.
    /// @return Model info, or std::nullopt if no model is loaded.
    [[nodiscard]] auto modelInfo() const -> std::optional<LoadedModelInfo>;

    /// Returns the detected chat template format for the loaded model.
    [[nodiscard]] auto chatTemplateFormat() const noexcept -> ChatTemplateFormat;

  private:
    std::filesystem::path _modelDir;
    int32_t _gpuLayers = -1;
    bool _flashAttention = true;
    llama_model* _model = nullptr;
    LoadedModelInfo _info;
};

} // namespace endo::agent::local

#endif // ENDO_HAS_LOCAL_LLM
