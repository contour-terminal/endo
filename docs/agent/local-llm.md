---
title: Local LLM Inference
description: Run the Endo AI agent entirely offline using local GGUF models via llama.cpp.
---

# Local LLM Inference

Endo can run AI agent inference entirely on your machine using
[llama.cpp](https://github.com/ggml-org/llama.cpp). No API key, no internet connection,
and no data ever leaves your system. This is ideal for air-gapped environments,
privacy-sensitive workflows, or simply avoiding cloud API costs.

## Requirements

- Endo built with llama.cpp support (`ENDO_HAS_LOCAL_LLM=1` -- enabled by default when
  llama.cpp is available as a system package or via CPM)
- A GGUF model file (downloaded via `endo agent models download` or obtained separately)
- Sufficient RAM/VRAM for the chosen model (see [Curated Models](#curated-models) below)

## Quick Start

### 1. Download a Model

Use the built-in model manager to download a curated model:

```bash
endo agent models download qwen2.5-coder-7b
```

This downloads the model to `~/.local/share/endo/models/` (Linux),
`~/Library/Application Support/endo/models/` (macOS), or `%LOCALAPPDATA%\endo\models\`
(Windows).

### 2. Configure the Provider

Add to `~/.config/endo/init.endo`:

<!-- endo-no-check -->
```endo
agent_provider <- "local"
agent_local_model_path <- "~/.local/share/endo/models/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
```

### 3. Enter Agent Mode

Press `Ctrl+T` at the shell prompt. The agent loads the model and runs inference locally.
The first prompt may take a few seconds while the model loads into memory; subsequent
turns reuse the loaded model and benefit from KV cache.

---

## Model Management CLI

The `endo agent models` command manages GGUF models on your system.

### List Available Models

```bash
endo agent models list
```

Shows all curated models and their download status:

```
Available Models:

  Name                    Size      RAM       Status          Description
  ────────────────────────────────────────────────────────────────────────
  qwen2.5-coder-7b       4.4 GB    7.5 GB    downloaded      Fast coding model, 8 GB RAM
  qwen3-coder-30b        18.6 GB   24.0 GB   not installed   Balanced coding agent, 24 GB RAM
  llama3.3-70b           37.3 GB   44.7 GB   not installed   Strong general + coding, 48 GB RAM
  qwen3-235b-moe         44.7 GB   48.4 GB   not installed   Best coding (MoE, needs GPU), 52 GB RAM
```

Any GGUF files manually placed in the models directory also appear as "custom model"
entries.

### Download a Model

```bash
endo agent models download <name> [--quant Q4_K_M]
```

Downloads a curated model with a progress bar. The `--quant` flag selects the
quantization variant (defaults to `Q4_K_M`).

```
Downloading Qwen 2.5 Coder 7B (Q4_K_M, 4.4 GB)...
  [██████████████████████████████] 100%  4.4 GB / 4.4 GB

Downloaded: ~/.local/share/endo/models/qwen2.5-coder-7b-instruct-q4_k_m.gguf
```

### Show Model Details

```bash
endo agent models info <name>
```

Displays architecture, parameter count, capabilities, and available quantizations:

```
Qwen 2.5 Coder 7B

  Architecture:     qwen2
  Parameters:       7B
  Tool Use:         Yes
  Vision:           No

  Available quantizations:
    Q4_K_M    4.4 GB    7.5 GB RAM   (downloaded)
```

### Remove a Model

```bash
endo agent models remove <name>
```

Deletes the downloaded model file.

---

## Curated Models

These models are tested and recommended for use with Endo's agent mode:

| Name | Params | Q4_K_M Size | RAM Required | Use Case |
|------|--------|-------------|--------------|----------|
| `qwen2.5-coder-7b` | 7B | ~4.4 GB | 8 GB | Fast coding, resource-constrained systems |
| `deepseek-coder-v2-lite` | 16B (MoE, 2.4B active) | ~10.4 GB | 12 GB | Efficient MoE coding model, 128k context |
| `qwen3-coder-30b` | 30B | ~18.6 GB | 24 GB | Balanced coding agent |
| `llama3.3-70b` | 70B | ~37.3 GB | 48 GB | Strong general-purpose + coding |
| `qwen3-235b-moe` | 235B (MoE) | ~44.7 GB | 52 GB | Best coding quality, requires dedicated GPU |
| `deepseek-coder-v2-instruct` | 236B (MoE) | ~142.5 GB (4 parts) | 96 GB | Strongest MoE coding model, split download |

All curated models support tool use (file reading, editing, shell commands, etc.). The
Qwen 2.5 Coder 7B model is a good starting point for machines with 8+ GB of RAM.

The DeepSeek Coder V2 Lite is a Mixture-of-Experts (MoE) model with 16B total parameters
but only 2.4B active per token, offering strong coding performance with lower compute
requirements and a large 128k token context window.

The DeepSeek Coder V2 Instruct (236B) is distributed as 4 split GGUF files (~142.5 GB
total). The download command handles split files automatically -- downloading each part
sequentially with aggregate progress. If a download is interrupted, re-running the command
resumes from the last incomplete part. llama.cpp loads split GGUF files natively when given
the path to the first part.

!!! tip
    You are not limited to curated models. Any GGUF model can be used -- simply set
    `agent_local_model_path` to the file path. The chat template is auto-detected from
    GGUF metadata, or you can override it with `agent_local_chat_template`.

---

## Configuration Reference

All properties are set in `~/.config/endo/init.endo`:

<!-- endo-no-check -->
```endo
agent_provider <- "local"
agent_local_model_path <- "~/.local/share/endo/models/qwen2.5-coder-7b-instruct-q4_k_m.gguf"

# GPU offloading (-1 = all layers, 0 = CPU only)
agent_local_gpu_layers <- -1

# Context window (tokens)
agent_local_context_size <- 32768

# CPU threads (0 = auto-detect based on hardware)
agent_local_threads <- 0

# Batch size for prompt evaluation
agent_local_batch_size <- 512

# Sampling temperature (percentage: 70 = 0.7)
agent_local_temperature <- 70

# Flash attention (disable if unsupported by your hardware)
agent_local_flash_attention <- true

# Max output tokens per response
agent_local_max_tokens <- 4096

# Chat template override (empty = auto-detect)
agent_local_chat_template <- ""
```

### Property Details

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `agent_local_model_path` | string | *(empty)* | Path to the GGUF model file. Required for the local provider to activate. |
| `agent_local_model_dir` | string | `~/.local/share/endo/models/` | Directory used by `endo agent models` for downloads and discovery. |
| `agent_local_gpu_layers` | int | -1 | Number of model layers to offload to GPU. Use -1 to offload all layers (recommended with a compatible GPU), or 0 for CPU-only inference. |
| `agent_local_context_size` | int | 32768 | Maximum context window in tokens. Larger values use more memory. |
| `agent_local_threads` | int | 0 | Number of CPU threads for inference. 0 means auto-detect (typically uses all performance cores). |
| `agent_local_batch_size` | int | 512 | Number of tokens to process in a single batch during prompt evaluation. Larger values are faster but use more memory. |
| `agent_local_temperature` | int | 70 | Sampling temperature as a percentage (70 = 0.7). Lower values produce more deterministic output, higher values are more creative. |
| `agent_local_flash_attention` | bool | `true` | Enable flash attention for faster inference and lower memory usage. Disable if you experience crashes on unsupported hardware. |
| `agent_local_max_tokens` | int | 4096 | Maximum tokens the model generates per response. |
| `agent_local_chat_template` | string | *(empty)* | Override the chat template format. When empty, the template is auto-detected from GGUF model metadata. Supported values: `chatml`, `llama3`, `mistral`, `gemma`, `phi3`, `qwen2`. |

---

## GPU Acceleration

llama.cpp automatically detects and uses available GPU backends:

| Backend | Platform | Detection |
|---------|----------|-----------|
| **CUDA** | NVIDIA GPUs | Requires CUDA toolkit |
| **Vulkan** | AMD / NVIDIA / Intel GPUs | Requires Vulkan SDK |
| **Metal** | Apple Silicon (M1/M2/M3/M4) | Automatic on macOS |

When a GPU is available, set `agent_local_gpu_layers` to `-1` to offload all model layers.
For machines with limited VRAM, set it to a specific number (e.g., `20`) to offload only
some layers, keeping the rest in system RAM.

For CPU-only inference:

<!-- endo-no-check -->
```endo
agent_local_gpu_layers <- 0
```

---

## KV Cache and Multi-Turn Performance

The local provider maintains an incremental KV cache across conversation turns. On the
first turn, the entire prompt is processed (this may take a few seconds for large contexts).
On subsequent turns, only the new tokens since the last turn are evaluated -- the common
prefix is reused from cache. This makes multi-turn conversations significantly faster.

The cache is tied to the active agent session. Starting a new session or switching models
resets the cache.

---

## Using Custom (Non-Curated) Models

Any GGUF model can be used with the local provider. To use a model obtained from
[Hugging Face](https://huggingface.co/) or another source:

1. Place the `.gguf` file anywhere on your filesystem (or in the models directory).

2. Set the path in `init.endo`:

    <!-- endo-no-check -->
    ```endo
    agent_local_model_path <- "/path/to/your-model.gguf"
    ```

3. If the chat template is not auto-detected correctly, specify it manually:

    <!-- endo-no-check -->
    ```endo
    agent_local_chat_template <- "chatml"
    ```

    Supported templates: `chatml`, `llama3`, `mistral`, `gemma`, `phi3`, `qwen2`.

!!! note
    Tool use quality varies by model. The curated models listed above are tested for
    reliable tool call parsing. Other models may produce tool calls in unexpected formats.

---

## Troubleshooting

### "No provider authenticated" when using local

Ensure `agent_local_model_path` points to an existing `.gguf` file. The local provider
considers itself "authenticated" when the model path is non-empty and the file exists.

### Model fails to load

- Verify the file is a valid GGUF file (not a partial download -- re-download if in doubt).
- Check that you have enough RAM for the model (see the RAM column in
  [Curated Models](#curated-models)).
- On machines with less RAM than required, try a smaller quantization or a smaller model.

### Slow inference

- Enable GPU offloading: `agent_local_gpu_layers <- -1`
- Increase batch size: `agent_local_batch_size <- 1024`
- Reduce context size if you don't need long conversations: `agent_local_context_size <- 8192`
- Ensure flash attention is enabled: `agent_local_flash_attention <- true`

### Tool calls not working

Some models produce tool calls in non-standard formats. The local provider uses a
multi-strategy parser that tries: (1) XML `<tool_call>` tags, (2) JSON code blocks,
(3) inline JSON extraction, (4) plain text fallback. If your model consistently fails to
produce tool calls, try one of the [curated models](#curated-models) which are tested for
reliable tool use.

---

## Further Reading

- [Overview](index.md) -- What the agent is and how to get started
- [Configuration](configuration.md) -- `init.endo` reference, MCP servers, web search
- [Tools & Commands](tools.md) -- Built-in tools, slash commands, plan mode
