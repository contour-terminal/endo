---
title: Agent Configuration
description: Configure the Endo AI agent -- init.endo properties, API key management, MCP servers, and web search.
---

# Agent Configuration

The AI agent is configured through two mechanisms:

1. **`init.endo`** -- primary configuration via builtin properties (provider, model, limits, MCP servers, web search).
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
`init.endo` (e.g., `agent_claude_api_key <- "sk-ant-..."`) or via environment variables
(`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`).

!!! note
    For each provider, the resolution order is: stored API key > environment variable.
    If both are present, the stored key takes precedence.

---

## Agent Settings (init.endo)

All properties are set from `~/.config/endo/init.endo` using `<-` assignment syntax.
They execute at shell startup and configure the agent before you enter agent mode.
Properties can also be read as expressions (e.g., `print agent_provider`).

### General

| Property | Type | Description |
|----------|------|-------------|
| `agent_provider` | string | Active provider: `"claude"`, `"openai"`, `"gemini"`, `"openai_compat"`, `"local"`. If not set, auto-detects from authenticated providers. |
| `agent_prompt_indicator` | string | Character(s) shown at the agent prompt (default: `"❯"`) |
| `agent_max_tool_result_size` | int | Max bytes from a single tool call before truncation (default: 30720) |
| `agent_log_tool_uses` | bool | Print tool invocations to the terminal (default: `true`) |

### Claude (Anthropic)

| Property | Type | Description |
|----------|------|-------------|
| `agent_claude_api_key` | string | API key (alternative to `endo agent login claude`) |
| `agent_claude_api_key_env` | string | Environment variable holding the API key (default: `"ANTHROPIC_API_KEY"`) |
| `agent_claude_model` | string | Model identifier (default: `"claude-sonnet-4-6"`) |
| `agent_claude_max_tokens` | int | Maximum output tokens per request (default: 8192) |
| `agent_claude_thinking_mode` | string | Thinking/reasoning mode: `"off"`, `"normal"`, `"extended"` (default: `"normal"`) |
| `agent_claude_prompt_caching` | bool | Enable prompt caching (default: `true`) |
| `agent_claude_auth_type` | string | Auth method: `"auto"` (default), `"oauth"`, `"api_key"` |

### OpenAI

| Property | Type | Description |
|----------|------|-------------|
| `agent_openai_api_key` | string | API key (alternative to `endo agent login openai`) |
| `agent_openai_api_key_env` | string | Environment variable holding the API key (default: `"OPENAI_API_KEY"`) |
| `agent_openai_model` | string | Model identifier (default: `"gpt-4o"`) |
| `agent_openai_base_url` | string | Custom base URL |
| `agent_openai_max_tokens` | int | Maximum output tokens per request (default: 4096) |
| `agent_openai_thinking_mode` | string | Thinking/reasoning mode (default: `"off"`) |

### OpenAI-Compatible

| Property | Type | Description |
|----------|------|-------------|
| `agent_openai_compat_api_key` | string | API key |
| `agent_openai_compat_api_key_env` | string | Environment variable holding the API key |
| `agent_openai_compat_model` | string | Model identifier |
| `agent_openai_compat_base_url` | string | Endpoint base URL (e.g. `"http://localhost:11434/v1"`) |
| `agent_openai_compat_max_tokens` | int | Maximum output tokens per request (default: 4096) |
| `agent_openai_compat_thinking_mode` | string | Thinking/reasoning mode (default: `"off"`) |

### Google Gemini

| Property | Type | Description |
|----------|------|-------------|
| `agent_gemini_api_key` | string | API key (alternative to `endo agent login gemini`) |
| `agent_gemini_api_key_env` | string | Environment variable holding the API key (default: `"GEMINI_API_KEY"`) |
| `agent_gemini_model` | string | Model identifier (default: `"gemini-2.5-flash"`) |
| `agent_gemini_max_tokens` | int | Maximum output tokens per request (default: 8192) |
| `agent_gemini_thinking_mode` | string | Thinking/reasoning mode (default: `"off"`) |

### Local (llama.cpp)

The local provider runs inference on your machine using GGUF models via llama.cpp. No API
key is required -- authentication is based on having a valid model path configured.
See [Local LLM Inference](local-llm.md) for the full setup guide.

| Property | Type | Description |
|----------|------|-------------|
| `agent_local_model_path` | string | Path to the GGUF model file (required for local provider) |
| `agent_local_model_dir` | string | Directory for model storage (default: `"~/.local/share/endo/models/"`) |
| `agent_local_gpu_layers` | int | GPU layers to offload (-1 = all available, 0 = CPU only; default: -1) |
| `agent_local_context_size` | int | Context window size in tokens (default: 32768) |
| `agent_local_threads` | int | CPU threads for inference (0 = auto-detect; default: 0) |
| `agent_local_batch_size` | int | Batch size for prompt evaluation (default: 512) |
| `agent_local_temperature` | int | Sampling temperature as percentage, e.g. 70 = 0.7 (default: 70) |
| `agent_local_flash_attention` | bool | Enable flash attention if supported by hardware (default: `true`) |
| `agent_local_max_tokens` | int | Maximum output tokens per request (default: 4096) |
| `agent_local_chat_template` | string | Chat template override (empty = auto-detect from GGUF metadata) |

### Plan Mode

| Property | Type | Description |
|----------|------|-------------|
| `agent_plan_mode_enabled` | bool | Whether `/plan` is available (default: `true`) |
| `agent_plan_mode_pause_between_steps` | bool | Pause for confirmation between plan steps (default: `false`) |
| `agent_plan_mode_max_exploration_turns` | int | Max exploration iterations before requiring a plan (default: 15) |

### Explore Sub-Agent

| Property | Type | Description |
|----------|------|-------------|
| `agent_explore_max_turns` | int | Maximum iterations for the explore sub-agent (default: 10) |

### Session

| Property | Type | Description |
|----------|------|-------------|
| `agent_auto_resume` | bool | Automatically resume the last agent session on startup (default: `false`) |
| `agent_session_replay` | bool | Replay session history when resuming (default: `false`) |

### Permissions

| Property | Type | Description |
|----------|------|-------------|
| `agent_permissions_policy` | string | Permission policy: `"ask"` (default), `"trust_session"`, `"trust_all"`, `"read_only"` |
| `agent_trusted_tool` | list | Tools auto-approved regardless of risk level |
| `agent_blocked_pattern` | list | Shell command patterns unconditionally blocked |

<!-- endo-no-check -->
```endo
agent_permissions_policy <- "ask"
agent_trusted_tool <- ["read_file"; "glob"; "grep"; "search"]
agent_blocked_pattern <- ["rm -rf /"; ":(){ :|:& };:"]
```

### Error Recovery

| Property | Type | Description |
|----------|------|-------------|
| `agent_error_recovery_action` | string | Action when a shell command fails: `"ask"`, `"analyze"`, `"ignore"` (default) |
| `agent_error_recovery_model` | string | Model for error analysis (empty = use active agent model) |

```endo
# Enable interactive error recovery prompt on command failure
agent_error_recovery_action <- "ask"

# Or automatically analyze failed commands without asking
# agent_error_recovery_action <- "analyze"

# Use a faster/cheaper model for error analysis
agent_error_recovery_model <- "claude-haiku-4-5-20251001"
```

By default, error recovery is disabled (`"ignore"`). Set to `"ask"` to show a prompt after each
failed command offering to analyze the error, or `"analyze"` to automatically analyze without
asking. When using `"ask"`, options include "Analyze (always)" and "Ignore (always)" to set
session-level overrides. Error analysis uses the Contour terminal's Semantic Block Query
extension (DEC Mode 2034) to capture the failed command's output. Non-Contour terminals
gracefully skip error recovery (no prompts appear).

### Tracing

| Property | Type | Description |
|----------|------|-------------|
| `agent_trace_enabled` | bool | Enable tool I/O trace logging (default: `false`) |
| `agent_trace_default_path` | string | Trace file path (empty = auto-generated) |
| `agent_trace_max_files` | int | Maximum number of auto-generated trace files to retain (default: 20). Oldest files are pruned when the limit is exceeded. Only applies when the trace path is auto-generated. |

!!! note
    When the trace path is auto-generated (i.e. `agent_trace_default_path` is empty), trace
    files are written to `<project-root>/.endo/trace-logs/` if a Git repository is detected,
    or `~/.local/state/endo/trace-logs/` otherwise.

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
agent_web_search_engine <- "duckduckgo"   # default, no key required
agent_web_search_engine <- "brave"
agent_web_search_engine <- "google"

# API key (required for Brave and Google)
agent_web_search_api_key <- "your-api-key"

# Maximum results per query (default: 5, max: 20)
agent_web_search_max_results <- 5

# Google Custom Search Engine ID (required for Google)
agent_web_search_cx <- "your-cx-id"
```

---

## Complete init.endo Example

<!-- endo-no-check -->
```endo
# ~/.config/endo/init.endo

# Agent provider and model (cloud)
agent_provider <- "claude"
agent_claude_model <- "claude-sonnet-4-6"
agent_log_tool_uses <- true

# -- Or use a local model instead --
# agent_provider <- "local"
# agent_local_model_path <- "~/.local/share/endo/models/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
# agent_local_gpu_layers <- -1
# agent_local_context_size <- 32768

# Plan mode
agent_plan_mode_enabled <- true
agent_plan_mode_max_exploration_turns <- 20

# Explore sub-agent
agent_explore_max_turns <- 15

# Permissions
agent_permissions_policy <- "ask"
agent_trusted_tool <- ["read_file"; "glob"; "grep"; "search"]

# Error recovery
agent_error_recovery_action <- "ask"

# Tracing
agent_trace_max_files <- 10

# Shell prompt
shell_prompt_preset <- "endo-signature"

# MCP servers
add_mcp_server "filesystem" "npx -y @modelcontextprotocol/server-filesystem /home/user"
add_mcp_server "github" "npx -y @modelcontextprotocol/server-github"
set_mcp_env "github" "GITHUB_TOKEN" "$GITHUB_TOKEN"

# Web search
agent_web_search_engine <- "duckduckgo"
```

## Further Reading

- [Overview](index.md) -- What the agent is and how to get started
- [Local LLM Inference](local-llm.md) -- Offline inference with llama.cpp, model management
- [Tools & Commands](tools.md) -- Built-in tools, slash commands, plan mode
