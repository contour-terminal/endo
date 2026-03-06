// SPDX-License-Identifier: Apache-2.0
#include "LlamaCppProvider.hpp"

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM

    #include <algorithm>
    #include <cstring>
    #include <format>
    #include <string>
    #include <utility>
    #include <vector>

    #include <llama.h>

    #include <agent/providers/local/ChatTemplate.hpp>
    #include <agent/providers/local/ToolCallParser.hpp>

namespace endo::agent
{

namespace
{
    /// Tokenizes a string using the model's tokenizer.
    [[nodiscard]] auto tokenize(llama_model const* model, std::string_view text, bool addBos)
        -> std::vector<int32_t>
    {
        auto const* vocab = llama_model_get_vocab(model);
        auto const nMax = static_cast<int32_t>(text.size()) + (addBos ? 1 : 0) + 16;
        auto tokens = std::vector<int32_t>(static_cast<size_t>(nMax));
        auto const n = llama_tokenize(
            vocab, text.data(), static_cast<int32_t>(text.size()), tokens.data(), nMax, addBos, true);
        if (n < 0)
        {
            // Retry with a larger buffer.
            tokens.resize(static_cast<size_t>(-n));
            auto const n2 = llama_tokenize(
                vocab, text.data(), static_cast<int32_t>(text.size()), tokens.data(), -n, addBos, true);
            if (n2 < 0)
                return {};
            tokens.resize(static_cast<size_t>(n2));
        }
        else
        {
            tokens.resize(static_cast<size_t>(n));
        }
        return tokens;
    }

    /// Detokenizes a single token to a string.
    [[nodiscard]] auto detokenize(llama_model const* model, int32_t token) -> std::string
    {
        auto const* vocab = llama_model_get_vocab(model);
        auto buf = std::array<char, 128> {};
        auto const n =
            llama_token_to_piece(vocab, token, buf.data(), static_cast<int32_t>(buf.size()), 0, true);
        if (n < 0)
            return {};
        return std::string(buf.data(), static_cast<size_t>(n));
    }

    /// Adds a token to a batch at the given position.
    void batchAdd(llama_batch& batch, int32_t tokenId, int32_t pos, bool logits)
    {
        auto const i = batch.n_tokens;
        batch.token[i] = tokenId;
        batch.pos[i] = pos;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = logits ? 1 : 0;
        ++batch.n_tokens;
    }

    /// Finds the length of the common prefix between two token sequences.
    [[nodiscard]] auto commonPrefixLength(std::span<int32_t const> a, std::span<int32_t const> b) -> size_t
    {
        auto const minLen = std::min(a.size(), b.size());
        for (size_t i = 0; i < minLen; ++i)
        {
            if (a[i] != b[i])
                return i;
        }
        return minLen;
    }
} // namespace

LlamaCppProvider::LlamaCppProvider(local::ModelManager& modelManager, LlamaCppProviderConfig config):
    _modelManager(modelManager), _config(config)
{
}

LlamaCppProvider::~LlamaCppProvider()
{
    if (_ctx)
        llama_free(_ctx);
}

LlamaCppProvider::LlamaCppProvider(LlamaCppProvider&& other) noexcept:
    _modelManager(other._modelManager),
    _config(other._config),
    _ctx(std::exchange(other._ctx, nullptr)),
    _cachedTokens(std::move(other._cachedTokens))
{
}

auto LlamaCppProvider::operator=(LlamaCppProvider&& other) noexcept -> LlamaCppProvider&
{
    if (this != &other)
    {
        if (_ctx)
            llama_free(_ctx);
        _config = other._config;
        _ctx = std::exchange(other._ctx, nullptr);
        _cachedTokens = std::move(other._cachedTokens);
    }
    return *this;
}

auto LlamaCppProvider::ensureContext() -> std::optional<std::string>
{
    if (_ctx)
        return std::nullopt;

    if (!_modelManager.isLoaded())
        return std::string("No model loaded");

    auto params = llama_context_default_params();
    params.n_ctx = static_cast<uint32_t>(_config.contextSize);
    params.n_batch = static_cast<uint32_t>(_config.batchSize);
    params.flash_attn = _config.flashAttention;

    if (_config.contextSize == 0)
        params.n_ctx = 0; // Use model default.

    _ctx = llama_init_from_model(_modelManager.model(), params);
    if (!_ctx)
        return std::string("Failed to create llama context");

    _cachedTokens.clear();
    return std::nullopt;
}

auto LlamaCppProvider::effectiveChatTemplate() const -> local::ChatTemplateFormat
{
    if (_config.useChatTemplateOverride)
        return _config.chatTemplateOverride;
    return _modelManager.chatTemplateFormat();
}

auto LlamaCppProvider::generate(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> tools,
                                StreamCallback streamCb) -> std::expected<GenerateResult, ProviderError>
{
    // Ensure context is ready.
    if (auto error = ensureContext())
    {
        return std::unexpected(ProviderError {
            .code = ProviderErrorCode::ConfigError,
            .message = std::move(*error),
        });
    }

    auto const* model = _modelManager.model();

    // Format prompt using the appropriate chat template.
    auto const prompt = local::formatPrompt(messages, tools, effectiveChatTemplate());

    // Tokenize the prompt.
    auto const promptTokens = tokenize(model, prompt, false);
    if (promptTokens.empty())
    {
        return std::unexpected(ProviderError {
            .code = ProviderErrorCode::InvalidResponse,
            .message = "Failed to tokenize prompt",
        });
    }

    // KV cache optimization: find common prefix with cached tokens.
    auto const commonPrefix = commonPrefixLength(_cachedTokens, promptTokens);

    // Remove divergent suffix from KV cache.
    if (commonPrefix < _cachedTokens.size())
        // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
        llama_kv_self_seq_rm(_ctx, 0, static_cast<int32_t>(commonPrefix), -1);

    // Decode only the new tokens (after the common prefix).
    auto const newTokensStart = commonPrefix;
    auto const nNewTokens = promptTokens.size() - newTokensStart;

    if (nNewTokens > 0)
    {
        // Evaluate new prompt tokens in batches.
        auto const batchSize = static_cast<size_t>(_config.batchSize);
        for (size_t i = 0; i < nNewTokens; i += batchSize)
        {
            auto const nBatch = std::min(batchSize, nNewTokens - i);
            auto batch = llama_batch_init(static_cast<int32_t>(nBatch), 0, 1);

            for (size_t j = 0; j < nBatch; ++j)
            {
                auto const tokenIdx = newTokensStart + i + j;
                batchAdd(batch,
                         promptTokens[tokenIdx],
                         static_cast<int32_t>(commonPrefix + i + j),
                         (i + j == nNewTokens - 1)); // logits only for last token
            }

            if (llama_decode(_ctx, batch) != 0)
            {
                llama_batch_free(batch);
                return std::unexpected(ProviderError {
                    .code = ProviderErrorCode::ServerError,
                    .message = "llama_decode failed during prompt evaluation",
                });
            }
            llama_batch_free(batch);
        }
    }

    // Autoregressive generation loop.
    auto generatedTokens = std::vector<int32_t> {};
    auto generatedText = std::string {};
    auto const stopStrings = local::stopTokens(effectiveChatTemplate());
    auto const maxGen = _config.maxTokens;
    auto const* vocab = llama_model_get_vocab(model);
    // Set up sampler chain.
    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(_config.topK));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(_config.topP, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(_config.temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(0));

    auto nCurPos = static_cast<int32_t>(promptTokens.size());

    for (size_t nGen = 0; nGen < maxGen; ++nGen)
    {
        // Sample next token.
        auto const newToken = llama_sampler_sample(smpl, _ctx, -1);

        // Check for EOS.
        if (llama_vocab_is_eog(vocab, newToken))
            break;

        // Detokenize.
        auto const tokenStr = detokenize(model, newToken);
        generatedText += tokenStr;
        generatedTokens.push_back(newToken);

        // Check stop strings.
        auto hitStop = false;
        for (auto const& stop: stopStrings)
        {
            if (generatedText.ends_with(stop))
            {
                // Remove the stop string from output.
                generatedText.erase(generatedText.size() - stop.size());
                hitStop = true;
                break;
            }
        }
        if (hitStop)
            break;

        // Stream callback.
        if (streamCb && !tokenStr.empty())
        {
            if (!streamCb(tokenStr))
                break; // User aborted.
        }

        // Decode the new token for the next iteration.
        auto batch = llama_batch_init(1, 0, 1);
        batchAdd(batch, newToken, nCurPos, true);
        ++nCurPos;

        if (llama_decode(_ctx, batch) != 0)
        {
            llama_batch_free(batch);
            llama_sampler_free(smpl);
            return std::unexpected(ProviderError {
                .code = ProviderErrorCode::ServerError,
                .message = "llama_decode failed during generation",
            });
        }
        llama_batch_free(batch);
    }

    llama_sampler_free(smpl);

    // Update cached tokens for KV cache reuse in next turn.
    _cachedTokens = promptTokens;
    _cachedTokens.insert(_cachedTokens.end(), generatedTokens.begin(), generatedTokens.end());

    // Build result.
    auto result = GenerateResult {};
    result.usage = TokenUsage {
        .inputTokens = static_cast<int64_t>(nNewTokens),
        .outputTokens = static_cast<int64_t>(generatedTokens.size()),
        .cacheReadTokens = static_cast<int64_t>(commonPrefix),
    };

    // Parse tool calls if tools were provided.
    if (!tools.empty())
    {
        auto parsed = local::parseToolCalls(generatedText, tools);
        result.toolCalls = std::move(parsed.toolCalls);
        if (!parsed.textContent.empty())
            result.content.emplace_back(TextBlock { .text = std::move(parsed.textContent) });
    }
    else
    {
        result.content.emplace_back(TextBlock { .text = std::move(generatedText) });
    }

    return result;
}

auto LlamaCppProvider::supportsToolUse() const noexcept -> bool
{
    if (auto info = _modelManager.modelInfo())
        return info->supportsToolUse;
    return false;
}

auto LlamaCppProvider::supportsImageInput() const noexcept -> bool
{
    if (auto info = _modelManager.modelInfo())
        return info->supportsVision;
    return false;
}

auto LlamaCppProvider::supportsImageOutput() const noexcept -> bool
{
    return false; // Local models don't generate images.
}

auto LlamaCppProvider::contextSize() const noexcept -> size_t
{
    return _config.contextSize;
}

auto LlamaCppProvider::modelInfo() const -> ModelInfo
{
    auto info = _modelManager.modelInfo();
    return ModelInfo {
        .providerName = "local",
        .modelName = info ? info->name : "unknown",
        .contextSize = _config.contextSize,
        .supportsToolUse = supportsToolUse(),
        .supportsImageInput = supportsImageInput(),
        .supportsImageOutput = false,
    };
}

} // namespace endo::agent

#endif // ENDO_HAS_LOCAL_LLM
