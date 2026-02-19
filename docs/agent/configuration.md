---
title: Agent Configuration
description: Configure the Endo AI agent -- init.endo builtins, API key management, MCP servers, and web search.
---

# Agent Configuration

The AI agent is configured through two mechanisms:

1. **`init.endo`** -- primary configuration via shell builtins (provider, model, limits, MCP servers, web search).
2. **`agent.yml`** -- API key store managed exclusively by `endo agent login` / `endo agent logout`.

All agent settings (provider selection, model, limits, plan mode, tracing) are configured in
`~/.config/endo/init.endo`. The `agent.yml` file only stores API keys.

## Authentication

Manage API keys from the terminal:

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

API keys are stored in `~/.config/endo/agent.yml`. You can also set keys directly in
`init.endo` (e.g., `set_claude_api_key "sk-ant-..."`) or via environment variables
(`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`).

!!! note
    For each provider, the resolution order is: stored API key > environment variable.
    If both are present, the stored key takes precedence.

---

## Agent Settings (init.endo)

All builtins are called from `~/.config/endo/init.endo`. They execute at shell startup and
configure the agent before you enter agent mode.

### General

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_agent_provider` | `name` | Active provider: `"claude"`, `"openai"`, `"gemini"`, `"openai_compat"`. If not set, auto-detects from authenticated providers. |
| `set_agent_prompt_indicator` | `chars` | Character(s) shown at the agent prompt (default: `"❯"`) |
| `set_agent_max_tool_result_size` | `bytes` | Max bytes from a single tool call before truncation (default: 30720) |
| `set_agent_log_tool_uses` | `true`\|`false` | Print tool invocations to the terminal (default: `true`) |

### Claude (Anthropic)

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_claude_api_key` | `key` | API key (alternative to `endo agent login claude`) |
| `set_claude_api_key_env` | `env_var` | Environment variable holding the API key (default: `"ANTHROPIC_API_KEY"`) |
| `set_claude_model` | `model` | Model identifier (default: `"claude-sonnet-4-5-20250929"`) |
| `set_claude_max_tokens` | `n` | Maximum output tokens per request (default: 8192) |

### OpenAI

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_openai_api_key` | `key` | API key (alternative to `endo agent login openai`) |
| `set_openai_api_key_env` | `env_var` | Environment variable holding the API key (default: `"OPENAI_API_KEY"`) |
| `set_openai_model` | `model` | Model identifier (default: `"gpt-4o"`) |
| `set_openai_base_url` | `url` | Custom base URL |
| `set_openai_max_tokens` | `n` | Maximum output tokens per request (default: 4096) |

### OpenAI-Compatible

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_openai_compat_api_key` | `key` | API key |
| `set_openai_compat_api_key_env` | `env_var` | Environment variable holding the API key |
| `set_openai_compat_model` | `model` | Model identifier |
| `set_openai_compat_base_url` | `url` | Endpoint base URL (e.g. `"http://localhost:11434/v1"`) |
| `set_openai_compat_max_tokens` | `n` | Maximum output tokens per request (default: 4096) |

### Google Gemini

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_gemini_api_key` | `key` | API key (alternative to `endo agent login gemini`) |
| `set_gemini_api_key_env` | `env_var` | Environment variable holding the API key (default: `"GEMINI_API_KEY"`) |
| `set_gemini_model` | `model` | Model identifier (default: `"gemini-2.5-flash"`) |
| `set_gemini_max_tokens` | `n` | Maximum output tokens per request (default: 8192) |

### Plan Mode

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_plan_mode_enabled` | `true`\|`false` | Whether `/plan` is available (default: `true`) |
| `set_plan_mode_pause_between_steps` | `true`\|`false` | Pause for confirmation between plan steps (default: `false`) |
| `set_plan_mode_max_exploration_turns` | `n` | Max exploration iterations before requiring a plan (default: 15) |

### Explore Sub-Agent

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_explore_max_turns` | `n` | Maximum iterations for the explore sub-agent (default: 10) |

### Tracing

| Builtin | Arguments | Description |
|---------|-----------|-------------|
| `set_trace_enabled` | `true`\|`false` | Enable tool I/O trace logging (default: `false`) |
| `set_trace_default_path` | `path` | Trace file path (empty = auto-generated) |

---

## MCP Server Configuration

[Model Context Protocol (MCP)](https://modelcontextprotocol.io/) servers expose external
tools to the agent over a JSON-RPC 2.0 stdio transport. Configure them in
`~/.config/endo/init.endo` using three builtins:

### Adding a Server

```endo
add_mcp_server "filesystem" "npx -y @modelcontextprotocol/server-filesystem /home/user"
add_mcp_server "github" "npx -y @modelcontextprotocol/server-github"
```

The first argument is a name (used for identification and `set_mcp_env`). The second is the
command line -- Endo splits it on spaces into executable and arguments.

### Setting Environment Variables

Some MCP servers need API keys or other environment variables:

```endo
set_mcp_env "github" "GITHUB_TOKEN" "$GITHUB_TOKEN"
```

### Removing a Server

```endo
remove_mcp_server "filesystem"
```

### Lifecycle

MCP servers are started when you enter agent mode (`Ctrl+T`) and shut down when you leave.
Each server's tools appear alongside the built-in tools -- the agent can call them
transparently.

If a server fails to start, the error is logged but agent mode still activates with the
remaining tools.

!!! tip
    Run `endo agent status` to see which providers are authenticated.

---

## Web Search Configuration

The agent's `web_search` tool can use DuckDuckGo (default, no API key), Brave Search, or
Google Custom Search. Configure it in `init.endo`:

```endo
# Select search engine
set_web_search_engine "duckduckgo"   # default, no key required
set_web_search_engine "brave"
set_web_search_engine "google"

# API key (required for Brave and Google)
set_web_search_api_key "your-api-key"

# Maximum results per query (default: 5, max: 20)
set_web_search_max_results 5

# Google Custom Search Engine ID (required for Google)
set_web_search_cx "your-cx-id"
```

---

## Complete init.endo Example

```endo
# ~/.config/endo/init.endo

# Agent provider and model
set_agent_provider "claude"
set_claude_model "claude-sonnet-4-5-20250929"
set_agent_log_tool_uses true

# Plan mode
set_plan_mode_enabled true
set_plan_mode_max_exploration_turns 20

# Explore sub-agent
set_explore_max_turns 15

# Shell prompt
set_prompt_preset "endo-signature"

# MCP servers
add_mcp_server "filesystem" "npx -y @modelcontextprotocol/server-filesystem /home/user"
add_mcp_server "github" "npx -y @modelcontextprotocol/server-github"
set_mcp_env "github" "GITHUB_TOKEN" "$GITHUB_TOKEN"

# Web search
set_web_search_engine "duckduckgo"
```

## Further Reading

- [Overview](index.md) -- What the agent is and how to get started
- [Tools & Commands](tools.md) -- Built-in tools, slash commands, plan mode
