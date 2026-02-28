---
title: AI Agent Overview
description: LLM-powered coding assistant built into the Endo shell -- setup, providers, and quick start.
---

# AI Agent Overview

Endo includes a built-in AI agent -- an LLM-powered coding assistant that lives inside your
shell. It can read and edit files, run commands, search your codebase, execute Endo scripts,
and connect to external tool servers via [MCP](configuration.md#mcp-server-configuration).

## Quick Start

1. **Log in** to a provider:

    ```bash
    endo agent login
    ```

    This walks you through selecting a provider and entering an API key. The key is stored
    in `~/.config/endo/agent.yml`.

2. **Configure the provider** in `~/.config/endo/init.endo`:

    ```endo
    set_agent_provider "claude"
    ```

    If you only have one provider authenticated, this step is optional -- Endo auto-detects it.

3. **Enter agent mode** by pressing `Ctrl+T` at the shell prompt.

4. **Ask a question** -- the agent can read files, run commands, and make edits on your
   behalf.

5. **Exit** with `Ctrl+D` or `/exit`.

## Supported Providers

| Provider | Default Model | Context Window |
|----------|---------------|----------------|
| **Claude** (Anthropic) | `claude-sonnet-4-6` | 200,000 tokens |
| **OpenAI** | `gpt-4o` | 128,000 tokens |
| **Google Gemini** | `gemini-2.5-flash` | 1,000,000 tokens |
| **OpenAI-compatible** | `gpt-4o` | 128,000 tokens |
| **Local** (llama.cpp) | Any GGUF model | Configurable (default: 32,768) |

The OpenAI-compatible provider works with local inference servers such as Ollama, vLLM,
and LM Studio.

The **local** provider runs inference entirely on your machine via
[llama.cpp](https://github.com/ggml-org/llama.cpp) -- no API key or internet connection
required. See [Local LLM Inference](local-llm.md) for setup instructions.

## CLI Commands

Manage provider authentication from the terminal without entering agent mode:

```bash
# Interactive login (select provider, enter API key)
endo agent login
endo agent login claude

# Show configured providers and authentication status
endo agent status

# Remove a stored API key
endo agent logout
endo agent logout gemini
```

### Local Model Management

Download, list, and manage GGUF models for local inference:

```bash
# List available and downloaded models
endo agent models list

# Download a curated model
endo agent models download qwen2.5-coder-7b

# Show detailed model information
endo agent models info qwen2.5-coder-7b

# Remove a downloaded model
endo agent models remove qwen2.5-coder-7b
```

See [Local LLM Inference](local-llm.md) for the full guide.

## Entering and Exiting Agent Mode

| Action | Key / Command |
|--------|---------------|
| Enter agent mode | `Ctrl+T` (default, customizable via keybindings) |
| Exit agent mode | `Ctrl+D` or `/exit` |

Once inside agent mode the prompt changes to the configured indicator (default `❯`) and
all input is sent to the LLM. Shell commands and F# expressions are no longer executed
directly -- the agent decides when to invoke tools on your behalf.

!!! tip
    You can customize the agent prompt indicator in
    [`init.endo`](configuration.md#general):
    `set_agent_prompt_indicator ">"`.

## Further Reading

- [Configuration](configuration.md) -- `init.endo` reference, MCP servers, web search
- [Local LLM Inference](local-llm.md) -- Offline inference with llama.cpp, model management
- [Tools & Commands](tools.md) -- Built-in tools, slash commands, plan mode
