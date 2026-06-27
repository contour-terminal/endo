// SPDX-License-Identifier: Apache-2.0
#include "ModelManager.hpp"

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM

    #include <algorithm>
    #include <array>
    #include <cstring>
    #include <format>
    #include <string>
    #include <utility>

    #include <ggml.h>
    #include <llama.h>

    #include <agent/tracing/AgentTracer.hpp>

namespace endo::agent::local
{

namespace
{
    /// Detects the chat template format from GGUF metadata.
    [[nodiscard]] auto detectChatTemplate(llama_model const* model) -> ChatTemplateFormat
    {
        // llama.cpp provides the chat template string in GGUF metadata.
        auto buf = std::array<char, 2048> {};
        auto const len = llama_model_meta_val_str(model, "tokenizer.chat_template", buf.data(), buf.size());
        if (len <= 0)
            return ChatTemplateFormat::Generic;

        auto const tmpl = std::string_view(buf.data(), static_cast<size_t>(len));

        // Heuristic detection based on template content.
        if (tmpl.find("<|im_start|>") != std::string_view::npos)
            return ChatTemplateFormat::ChatML;
        if (tmpl.find("<|start_header_id|>") != std::string_view::npos)
            return ChatTemplateFormat::Llama3;
        if (tmpl.find("[INST]") != std::string_view::npos)
            return ChatTemplateFormat::Mistral;
        if (tmpl.find("<start_of_turn>") != std::string_view::npos)
            return ChatTemplateFormat::Gemma;
        if (tmpl.find("<|system|>") != std::string_view::npos)
            return ChatTemplateFormat::Phi3;

        return ChatTemplateFormat::Generic;
    }

    /// Detects if the model architecture is known to support tool use.
    [[nodiscard]] auto detectToolUseSupport(std::string_view arch) -> bool
    {
        // Models known to support tool calling well.
        static constexpr auto ToolUseArchitectures = std::array {
            std::string_view { "llama" },     std::string_view { "qwen2" },
            std::string_view { "qwen2_moe" }, std::string_view { "mistral" },
            std::string_view { "command-r" },
        };
        return std::ranges::any_of(ToolUseArchitectures, [&](auto const& a) { return a == arch; });
    }

    /// Reads a string metadata value from the model.
    [[nodiscard]] auto readMetaString(llama_model const* model, char const* key) -> std::string
    {
        auto buf = std::array<char, 256> {};
        auto const len = llama_model_meta_val_str(model, key, buf.data(), buf.size());
        if (len <= 0)
            return {};
        return std::string(buf.data(), static_cast<size_t>(len));
    }

    /// Callback for redirecting llama.cpp log output to a trace log file.
    void llamaLogCallback(ggml_log_level /*level*/, char const* text, void* userData)
    {
        auto* stream = static_cast<std::ofstream*>(userData);
        if (stream && stream->is_open())
            *stream << text << std::flush;
    }
} // namespace

ModelManager::ModelManager(std::filesystem::path modelDir, int32_t gpuLayers, bool flashAttention):
    _modelDir(std::move(modelDir)), _gpuLayers(gpuLayers), _flashAttention(flashAttention)
{
    // Redirect llama.cpp log output to a trace log file to avoid corrupting the TUI.
    auto const logDir = endo::agent::resolveTraceLogDirectory();
    std::filesystem::create_directories(logDir);
    _logStream.open(logDir / "llama.log", std::ios::app);
    llama_log_set(&llamaLogCallback, &_logStream);

    // Initialize llama.cpp backend (safe to call multiple times).
    llama_backend_init();
}

ModelManager::~ModelManager()
{
    unloadModel();
}

ModelManager::ModelManager(ModelManager&& other) noexcept:
    _modelDir(std::move(other._modelDir)),
    _gpuLayers(other._gpuLayers),
    _flashAttention(other._flashAttention),
    _model(std::exchange(other._model, nullptr)),
    _info(std::move(other._info)),
    _logStream(std::move(other._logStream))
{
    // Re-register callback to point at our _logStream (moved-from other's is invalid).
    llama_log_set(&llamaLogCallback, &_logStream);
}

auto ModelManager::operator=(ModelManager&& other) noexcept -> ModelManager&
{
    if (this != &other)
    {
        unloadModel();
        _modelDir = std::move(other._modelDir);
        _gpuLayers = other._gpuLayers;
        _flashAttention = other._flashAttention;
        _model = std::exchange(other._model, nullptr);
        _info = std::move(other._info);
        _logStream = std::move(other._logStream);
        llama_log_set(&llamaLogCallback, &_logStream);
    }
    return *this;
}

auto ModelManager::loadModel(std::filesystem::path const& path) -> std::optional<std::string>
{
    // Unload any previously loaded model.
    unloadModel();

    if (!std::filesystem::exists(path))
        return std::format("Model file not found: {}", path.string());

    // Configure model loading parameters.
    auto params = llama_model_default_params();
    params.n_gpu_layers = _gpuLayers;

    // Load the model.
    _model = llama_model_load_from_file(path.string().c_str(), params);
    if (!_model)
        return std::format("Failed to load model: {}", path.string());

    // Populate model info.
    _info.path = path;
    _info.name = readMetaString(_model, "general.name");
    _info.architecture = readMetaString(_model, "general.architecture");
    _info.fileSizeBytes = std::filesystem::file_size(path);
    _info.contextLength = static_cast<size_t>(llama_model_n_ctx_train(_model));
    _info.chatTemplate = detectChatTemplate(_model);
    _info.supportsToolUse = detectToolUseSupport(_info.architecture);

    // Check for vision encoder (multimodal models).
    _info.supportsVision = false; // Placeholder — vision detection TBD.

    if (_info.name.empty())
        _info.name = path.stem().string();

    return std::nullopt;
}

void ModelManager::unloadModel()
{
    if (_model)
    {
        llama_model_free(_model);
        _model = nullptr;
        _info = {};
    }
}

auto ModelManager::model() const noexcept -> llama_model*
{
    return _model;
}

auto ModelManager::isLoaded() const noexcept -> bool
{
    return _model != nullptr;
}

auto ModelManager::modelInfo() const -> std::optional<LoadedModelInfo>
{
    if (!_model)
        return std::nullopt;
    return _info;
}

auto ModelManager::chatTemplateFormat() const noexcept -> ChatTemplateFormat
{
    return _info.chatTemplate;
}

} // namespace endo::agent::local

#endif // ENDO_HAS_LOCAL_LLM
