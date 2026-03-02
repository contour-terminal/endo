// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM

    #include <cstddef>
    #include <cstdint>
    #include <string>
    #include <vector>

    #include <agent/providers/LlmProvider.hpp>
    #include <agent/providers/local/ChatTemplate.hpp>
    #include <agent/providers/local/ModelManager.hpp>

// Forward declarations for llama.cpp types.
struct llama_context;

namespace endo::agent
{

/// Configuration for the local llama.cpp provider.
struct LlamaCppProviderConfig
{
    size_t contextSize = 32768; ///< Context window size in tokens.
    size_t batchSize = 512;     ///< Batch size for prompt evaluation.
    float temperature = 0.7f;   ///< Sampling temperature.
    float topP = 0.9f;          ///< Top-p (nucleus) sampling.
    int32_t topK = 40;          ///< Top-k sampling.
    float repeatPenalty = 1.1f; ///< Repetition penalty.
    size_t maxTokens = 4096;    ///< Maximum output tokens per request.
    bool flashAttention = true; ///< Enable flash attention if supported.
    local::ChatTemplateFormat chatTemplateOverride = local::ChatTemplateFormat::Generic;
    bool useChatTemplateOverride = false; ///< Whether chatTemplateOverride should be used.
};

/// LLM provider implementation using llama.cpp for local inference.
///
/// Runs inference in-process using a shared ModelManager for model weights.
/// Each provider instance owns its own llama_context for thread safety.
/// Supports incremental KV cache across turns for fast multi-turn conversation.
class LlamaCppProvider final: public LlmProvider
{
  public:
    /// Constructs a provider using a shared model manager.
    /// @param modelManager Reference to the model manager (must outlive this provider).
    /// @param config Provider configuration (sampling params, context size, etc.).
    LlamaCppProvider(local::ModelManager& modelManager, LlamaCppProviderConfig config);

    ~LlamaCppProvider() override;

    LlamaCppProvider(LlamaCppProvider const&) = delete;
    auto operator=(LlamaCppProvider const&) -> LlamaCppProvider& = delete;
    LlamaCppProvider(LlamaCppProvider&&) noexcept;
    auto operator=(LlamaCppProvider&&) noexcept -> LlamaCppProvider&;

    /// Generates a response from the local model.
    [[nodiscard]] auto generate(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> tools,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override;

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override;
    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override;
    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override;
    [[nodiscard]] auto contextSize() const noexcept -> size_t override;
    [[nodiscard]] auto modelInfo() const -> ModelInfo override;

  private:
    /// Ensures the llama_context is created (lazy initialization).
    [[nodiscard]] auto ensureContext() -> std::optional<std::string>;

    /// Returns the effective chat template format.
    [[nodiscard]] auto effectiveChatTemplate() const -> local::ChatTemplateFormat;

    local::ModelManager& _modelManager;
    LlamaCppProviderConfig _config;
    llama_context* _ctx = nullptr;

    /// KV cache tracking: tokens currently in the KV cache.
    std::vector<int32_t> _cachedTokens;
};

} // namespace endo::agent

#endif // ENDO_HAS_LOCAL_LLM
