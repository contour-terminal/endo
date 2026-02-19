---
title: Agent Configuration
description: Configure the Endo AI agent -- agent.yml reference, MCP servers, and web search.
---

# Agent Configuration

The AI agent is configured through two mechanisms:

1. **`agent.yml`** -- static settings (provider keys, models, limits).
2. **`init.endo`** -- runtime configuration via shell builtins (MCP servers, web search).

## agent.yml Reference

Location: `~/.config/endo/agent.yml`

### Top-Level Settings

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `active_provider` | string | `"claude"` | Active provider name (`claude`, `openai`, `gemini`, `openai_compat`) |
| `prompt_indicator` | string | `"❯"` | Character shown at the agent prompt |
| `max_tool_result_size` | integer | `30720` | Maximum bytes returned from a single tool call before truncation |
| `log_tool_uses` | boolean | `true` | Print tool invocations to the terminal |

### Provider Sections

Each provider has its own section. You only need to configure the providers you use.

#### Claude (Anthropic)

```yaml
claude:
  api_key: "sk-ant-..."
  # api_key_env: "ANTHROPIC_API_KEY"   # alternative: read key from env var
  model: "claude-sonnet-4-5-20250929"
  max_tokens: 8192
```

#### OpenAI

```yaml
openai:
  api_key: "sk-..."
  # api_key_env: "OPENAI_API_KEY"
  model: "gpt-4o"
  base_url: "https://api.openai.com/v1"
  max_tokens: 4096
```

#### Google Gemini

```yaml
gemini:
  api_key: "..."
  # api_key_env: "GEMINI_API_KEY"
  model: "gemini-2.5-flash"
  max_tokens: 8192
```

#### OpenAI-Compatible

Use this section for local or third-party OpenAI-compatible endpoints (Ollama, vLLM,
LM Studio):

```yaml
openai_compat:
  api_key: "..."
  model: "llama3"
  base_url: "http://localhost:11434/v1"
  max_tokens: 4096
```

!!! note
    For each provider you can supply the API key either inline (`api_key`) or via an
    environment variable (`api_key_env`). If both are present, `api_key` takes precedence.

### Plan Mode

```yaml
plan_mode:
  enabled: true              # Whether /plan is available
  pause_between_steps: false # Pause for confirmation between plan steps
  max_exploration_turns: 15  # Maximum exploration iterations before requiring a plan
```

### Explore Sub-Agent

```yaml
explore:
  max_turns: 10   # Maximum iterations for the explore sub-agent
```

### Tracing

```yaml
trace:
  enabled: false       # Enable trace logging
  default_path: ""     # Trace file path (empty = auto-generated)
```

### Complete Example

```yaml
active_provider: claude
prompt_indicator: "❯"
max_tool_result_size: 30720
log_tool_uses: true

claude:
  api_key_env: "ANTHROPIC_API_KEY"
  model: "claude-sonnet-4-5-20250929"
  max_tokens: 8192

openai:
  api_key_env: "OPENAI_API_KEY"
  model: "gpt-4o"
  max_tokens: 4096

gemini:
  api_key_env: "GEMINI_API_KEY"
  model: "gemini-2.5-flash"
  max_tokens: 8192

openai_compat:
  base_url: "http://localhost:11434/v1"
  model: "llama3"
  max_tokens: 4096

plan_mode:
  enabled: true
  pause_between_steps: false
  max_exploration_turns: 15

explore:
  max_turns: 10

trace:
  enabled: false
```

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

MCP servers are started when you enter agent mode (`/agent`) and shut down when you leave.
Each server's tools appear alongside the built-in tools -- the agent can call them
transparently.

If a server fails to start, the error is logged but agent mode still activates with the
remaining tools.

!!! tip
    Run `endo agent status` to see which MCP servers are configured and whether they
    started successfully.

### Example: Full init.endo with MCP

```endo
# ~/.config/endo/init.endo

# Shell prompt
set_prompt_preset "endo-signature"

# MCP servers
add_mcp_server "filesystem" "npx -y @modelcontextprotocol/server-filesystem /home/user"
add_mcp_server "github" "npx -y @modelcontextprotocol/server-github"
set_mcp_env "github" "GITHUB_TOKEN" "$GITHUB_TOKEN"

# Web search
set_web_search_engine "duckduckgo"
```

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

## Further Reading

- [Overview](index.md) -- What the agent is and how to get started
- [Tools & Commands](tools.md) -- Built-in tools, slash commands, plan mode
