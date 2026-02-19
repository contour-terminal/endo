---
title: Tools & Commands
description: Built-in agent tools, MCP tools, slash commands, and plan mode.
---

# Tools & Commands

The AI agent has a set of built-in tools it can invoke autonomously, plus any tools
contributed by [MCP servers](configuration.md#mcp-server-configuration).

## Built-in Tools

| Tool | Description |
|------|-------------|
| `read_file` | Read file contents with optional line offset and limit. Lines are numbered and long lines are truncated at 2000 characters. |
| `write_file` | Create or overwrite a file. Parent directories are created automatically. |
| `edit_file` | Exact string replacement in a file. Fails if the search string is ambiguous unless `replace_all` is set. |
| `glob` | Find files matching a glob pattern (`*`, `**`, `?`). Results sorted by modification time. |
| `grep` | Search file contents by regular expression. Supports path and glob filters. |
| `shell_execute` | Run a shell command. Output is truncated at 30 KB. Default timeout: 2 minutes. |
| `endo_execute` | Evaluate Endo source code and capture its output. Same limits as `shell_execute`. |
| `git` | Execute git operations. Read-only subcommands run automatically; destructive patterns are blocked. |
| `explore` | Spawn a read-only sub-agent to research the codebase. The sub-agent's conversation is discarded after it returns. |
| `submit_plan` | Submit a structured plan during plan mode exploration. |
| `save_memory` | Persist a memory file to `~/.config/endo/agent-memory/`. Memories are loaded into the system prompt on future sessions. |
| `web_search` | Search the web via DuckDuckGo, Brave, or Google. Configure in [`init.endo`](configuration.md#web-search-configuration). |

MCP tools from configured servers appear alongside these built-in tools and are callable
in the same way.

## Slash Commands

Slash commands are typed directly in the agent prompt:

| Command | Description |
|---------|-------------|
| `/help` | List available commands |
| `/plan [query]` | Enter plan mode (also activated with **Shift+Tab**) |
| `/exit` | Leave agent mode (same as `Ctrl+D`) |

## Plan Mode

Plan mode lets the agent explore your codebase before making changes, producing a
step-by-step plan for your review.

### Entering Plan Mode

- Type `/plan` followed by an optional query, or
- Press **Shift+Tab** to cycle into plan mode.

### How It Works

1. **Exploration** -- the agent uses read-only tools (`read_file`, `glob`, `grep`, `git`)
   to understand the codebase. It has a budget of exploration turns (default: 15,
   configurable via [`plan_mode.max_exploration_turns`](configuration.md#plan-mode)).

2. **Plan submission** -- the agent calls `submit_plan` with a structured plan containing:
    - A high-level summary
    - Ordered steps with descriptions, rationale, and file lists
    - Dependency graph between steps
    - Risk assessment and alternative approaches

3. **Review** -- the plan is presented for your approval. You can accept, reject, or ask
   for revisions.

4. **Execution** -- once approved, the agent carries out the plan step by step. If
   `pause_between_steps` is enabled in `agent.yml`, it pauses for confirmation after each
   step.

### Agent Memory

The `save_memory` tool lets the agent persist notes across sessions. Memory files are
stored in `~/.config/endo/agent-memory/` and automatically loaded into the system prompt
when you next enter agent mode.

This is useful for recording project conventions, architectural decisions, or debugging
insights that the agent should remember.

## Further Reading

- [Overview](index.md) -- What the agent is and how to get started
- [Configuration](configuration.md) -- `agent.yml` reference, MCP servers, web search
