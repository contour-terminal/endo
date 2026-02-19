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

2. **Enter agent mode** from the shell prompt:

    ```endo
    /agent
    ```

3. **Ask a question** -- the agent can read files, run commands, and make edits on your
   behalf.

4. **Exit** with `Ctrl+D` or `/exit`.

## Supported Providers

| Provider | Default Model | Context Window |
|----------|---------------|----------------|
| **Claude** (Anthropic) | `claude-sonnet-4-5-20250929` | 200,000 tokens |
| **OpenAI** | `gpt-4o` | 128,000 tokens |
| **Google Gemini** | `gemini-2.5-flash` | 1,000,000 tokens |
| **OpenAI-compatible** | `gpt-4o` | 128,000 tokens |

The OpenAI-compatible provider works with local inference servers such as Ollama, vLLM,
and LM Studio.

## CLI Commands

Manage provider authentication from the terminal without entering agent mode:

```bash
# Interactive login (select provider, enter API key)
endo agent login
endo agent login claude

# Show configured providers and active selection
endo agent status

# Switch the active provider
endo agent switch
endo agent switch openai

# Remove a stored API key
endo agent logout
endo agent logout gemini
```

## Entering and Exiting Agent Mode

| Action | Key / Command |
|--------|---------------|
| Enter agent mode | `/agent` |
| Exit agent mode | `Ctrl+D` or `/exit` |

Once inside agent mode the prompt changes to the configured indicator (default `❯`) and
all input is sent to the LLM. Shell commands and F# expressions are no longer executed
directly -- the agent decides when to invoke tools on your behalf.

!!! tip
    You can customize the agent prompt indicator in
    [`agent.yml`](configuration.md#agentyml-reference).

## Further Reading

- [Configuration](configuration.md) -- `agent.yml` reference, MCP servers, web search
- [Tools & Commands](tools.md) -- Built-in tools, slash commands, plan mode
