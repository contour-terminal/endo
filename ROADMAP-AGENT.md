# Agentic AI Integration Roadmap

> Making agentic AI a **first-class feature** of endo's interactive shell mode.
> The primary screen (inline rendering) is the main focus — no alt-screen context switch
> for the core experience. Alt-screen is available on demand for sub-agents or full-screen views.

---

## Design Principles

1. **Shell-native** — endo IS the shell. No separate `bash` tool needed; the agent calls
   `Shell::execute()` directly for command execution.
2. **Inline-first** — Agent responses render inline in the primary screen using the existing
   `Screen` (Inline viewport) and `MarkdownRenderer::beginStream()/feedToken()/endStream()`.
3. **Seamless activation** — `#` on an empty prompt activates agent mode; `Escape` returns to
   shell mode. No mode confusion, no context switch.
4. **Provider-agnostic** — Support Claude API, OpenAI-compatible endpoints (OpenAI, Ollama,
   vLLM, LM Studio), and future local models through a single provider interface.
5. **Multimodal** — Images are first-class content. Users paste images from the clipboard;
   LLM responses with images render inline via sixel. The existing sixel encoder, `Canvas::drawImage`,
   and cell pixel dimension queries provide the rendering foundation.
6. **Team-capable** — Multi-agent teams with role assignment, a leader agent for coordination,
   and structured message passing. Complex tasks are decomposed and parallelized across agents.

---

## Existing Infrastructure to Leverage

| Need | Already Exists | Location |
|------|---------------|----------|
| HTTP + streaming | `HttpClient` (libcurl, RAII) | `src/http/HttpClient.hpp` |
| Markdown streaming | `MarkdownRenderer::beginStream()/feedToken()/endStream()` | `src/tui/MarkdownRenderer.hpp` |
| Inline UI | `Screen` (Inline viewport, diff-based flush) | `src/tui/Screen.hpp` |
| Component system | `Component`, focus groups, overlays | `src/tui/Component.hpp` |
| Text input | `InputField` (multiline, undo, clipboard, kill ring) | `src/tui/InputField.hpp` |
| Completion | `CompletionPopup` + `CompletionProvider` | `src/tui/CompletionPopup.hpp` |
| Shell execution | `Shell::execute(string const& lineBuffer) -> int` | `src/shell/Shell.hpp` |
| Spinner animation | `tui::Spinner` (10+ styles, label support) | `src/tui/Spinner.hpp` |
| Git info | `GitModule` (branch, status, dirty/clean) | `src/shell/modules/GitModule.hpp` |
| YAML parsing | yaml-cpp | Already a dependency |
| Terminal suspend | `Prompt::ScopedSuspend` (RAII guard) | `src/shell/Prompt.hpp` |
| OSC 133 markers | `emitCommandStart()`/`emitCommandFinished()` | `src/shell/Shell.cpp` |
| Prompt actions | `PromptComponent::Action` enum + `processInput()` | `src/shell/PromptComponent.hpp` |
| Syntax highlighting | Token-based highlighting in prompt | `src/shell/PromptComponent.hpp` |
| Sixel encoder | `encodeSixel(ImageData)` — pure C++ median-cut quantization | `src/tui/Sixel.hpp` |
| Inline image rendering | `Canvas::drawImage()` → `Buffer::addImage()` → sixel flush | `src/tui/Canvas.hpp`, `src/tui/Buffer.hpp` |
| Cell pixel dimensions | `CSI 16 t` query → `CellSizeReport` event | `src/tui/platform/Terminal.cpp` |
| Sixel output | `TerminalOutput::writeSixel()` (POSIX + Win32) | `src/tui/TerminalOutput.hpp` |

---

## Files to Bootstrap from mychat

These mychat files (`~/projects/mychat/`) provide proven interfaces to adapt
(change namespace `mychat` → `endo::agent`, use `std::expected` instead of `Result`,
adapt to endo C++23 conventions):

| mychat file | Endo target | Adaptation notes |
|-------------|-------------|------------------|
| `src/core/Types.hpp` | `src/agent/Types.hpp` | `ChatMessage`, `ToolCall`, `ToolResult`, `ToolDefinition`, `GenerateResult` — core data model. Replace `Result<T>` with `std::expected<T, AgentError>`. |
| `src/agent/AgentLoop.hpp/cpp` | `src/agent/AgentSession.hpp/cpp` | Multi-step tool loop. Add `ToolRegistry` for built-in tools alongside MCP routing. Use `Shell::execute()` instead of `BashTool`. |
| `src/llm/ChatSession.hpp/cpp` | `src/agent/ConversationHistory.hpp/cpp` | Conversation history management. Add per-message token tracking and compaction support. |
| `src/mcp/Transport.hpp` | `src/agent/mcp/Transport.hpp` | Abstract MCP transport interface (clean, copy nearly verbatim). |
| `src/mcp/McpClient.hpp/cpp` | `src/agent/mcp/McpClient.hpp/cpp` | MCP protocol client. Replace `Result<T>` → `std::expected<T, McpError>`. |
| `src/mcp/ServerManager.hpp/cpp` | `src/agent/mcp/ServerManager.hpp/cpp` | Multi-server routing. Add built-in `ToolRegistry` integration for unified tool dispatch. |
| `src/mcp/StdioTransport.hpp/cpp` | `src/agent/mcp/StdioTransport.hpp/cpp` | Stdio-based MCP transport (pimpl). Minor adaptation. |
| `src/mcp/JsonRpc.hpp/cpp` | `src/agent/mcp/JsonRpc.hpp/cpp` | JSON-RPC 2.0 protocol handling (stateless helpers). |

---

## Phase 1: LLM Provider Abstraction (**COMPLETE**)

**Goal:** Create a provider interface that supports multiple LLM backends via streaming HTTP APIs.
Endo already has `HttpClient` with libcurl — this phase adds SSE streaming and the provider layer.

**Status:** Fully implemented in `src/agent/` with 62 test cases (265 assertions), all passing.

### 1.1 Provider Interface

```
src/agent/
├── Types.hpp                 # Core data model (from mychat Types.hpp)
├── LlmProvider.hpp           # Abstract provider interface
├── ClaudeProvider.hpp/.cpp   # Anthropic Claude API (streaming SSE)
├── OpenAiProvider.hpp/.cpp   # OpenAI-compatible API (OpenAI, Ollama, vLLM, LM Studio)
└── AgentConfig.hpp           # Configuration data model
```

**Interface contract** (`LlmProvider`):

- `generate(messages, tools, streamCb) -> std::expected<GenerateResult, ProviderError>` — streaming generation with tool definitions
- `supportsToolUse() -> bool` — whether the provider handles tool calls natively
- `supportsImageInput() -> bool` — whether the provider accepts image content blocks
- `supportsImageOutput() -> bool` — whether the provider can return image data (Gemini native, OpenAI via tool)
- `contextSize() -> size_t` — effective context window
- `modelInfo() -> ModelInfo` — model name, provider name, capabilities

### 1.2 Multimodal Content Model

`ChatMessage` must support interleaved text and image content, not just a flat string:

```cpp
/// A single content block within a message (text, image, tool use, or tool result).
using ContentBlock = std::variant<TextBlock, ImageBlock, ToolUseBlock, ToolResultBlock>;

struct TextBlock {
    std::string text;
};

struct ImageBlock {
    std::vector<std::uint8_t> data;  // Raw image bytes (PNG/JPEG)
    std::string mediaType;           // "image/png", "image/jpeg"
};

struct ChatMessage {
    Role role;
    std::vector<ContentBlock> content;  // Multimodal content (not just text)
    // ...
};
```

Each provider serializes `ImageBlock` according to its wire format:
- **Claude API:** `{ "type": "image", "source": { "type": "base64", "media_type": "...", "data": "..." } }`
- **OpenAI API:** `{ "type": "image_url", "image_url": { "url": "data:image/png;base64,..." } }`
- **Gemini API:** `{ "inlineData": { "mimeType": "...", "data": "..." } }`

### 1.3 Tool Call Wire Formats

Each provider has its own tool-calling convention:

| Provider | Format |
|----------|--------|
| Claude API | Structured `tool_use` content blocks in the response body |
| OpenAI API | `tool_calls` array in the assistant message with function name + JSON args |
| OpenAI-compatible | Same as OpenAI (Ollama, vLLM, LM Studio, etc.) |

The provider layer normalizes all formats into `GenerateResult { text, vector<ToolCall> }`.

### 1.4 Image Output Wire Formats

LLM-generated images arrive through different mechanisms per provider:

| Provider | Mechanism | Format |
|----------|-----------|--------|
| Claude API | Not supported (text only) | N/A |
| OpenAI Responses API | `image_generation` built-in tool result | Base64 PNG in tool result JSON |
| Gemini 2.0+/2.5 Flash | Native inline `inlineData` parts | Base64 PNG/JPEG in response parts |

The provider layer normalizes image output into `ImageBlock` entries within `GenerateResult::content`,
regardless of whether the image arrived as a native inline part (Gemini) or as a tool result (OpenAI).
Providers that don't support image output simply never produce `ImageBlock` entries.

### 1.5 SSE Streaming Extension for HttpClient

Extend the existing `HttpClient` to support SSE (Server-Sent Events):

- Add `executeStreaming(HttpRequest, sseCallback)` method
- Parse `data:` lines from the event stream
- Map streaming chunks to `StreamCallback` so the TUI renderer works unchanged
- Handle `[DONE]` sentinel (OpenAI) and `event: message_stop` (Claude)

### 1.6 Configuration

Configuration lives in `~/.config/endo/agent.yml` (parsed by existing yaml-cpp dependency):

```yaml
provider: claude                # or "openai", "openai-compat"
model: claude-sonnet-4-5-20250929

claude:
  api_key_env: ANTHROPIC_API_KEY  # env var containing the API key

openai:
  api_key_env: OPENAI_API_KEY
  model: gpt-4o

openai_compat:
  base_url: http://localhost:11434/v1
  api_key_env: OLLAMA_API_KEY     # optional
  model: qwen2.5-coder:32b
```

- Environment variable fallbacks: `$ANTHROPIC_API_KEY`, `$OPENAI_API_KEY`
- API keys are never logged or displayed

**Touches:** `src/http/HttpClient.hpp/cpp` (SSE extension), new `src/agent/` directory, `CMakeLists.txt`

---

## Phase 2: Agent Mode UX — Inline Experience

**Goal:** Provide a seamless inline agent experience activated from the shell prompt.
This is the core UX — the user stays in their terminal, responses stream inline.

### 2.1 Agent Mode Activation

Add `AgentMode` to the existing `PromptComponent::Action` enum:

```cpp
enum class Action
{
    None,
    Changed,
    Submit,
    Abort,
    Eof,
    ClearScreen,
    AgentMode,    // NEW: user pressed '#' on empty prompt
};
```

In `PromptComponent::processInput()`: when the input is empty and the key is `#`,
return `Action::AgentMode` instead of inserting the character.

### 2.2 Agent Input Component

Create `AgentInputComponent` — a styled variant of `PromptComponent` for agent queries:

- **Purple/magenta left bar** (vs. the shell prompt's blue bar) for visual distinction
- Same `InputField` infrastructure (multiline, undo, clipboard, kill ring)
- `Escape` returns `Action::Abort` → shell restores the normal prompt
- `Enter` submits the query to the agent session

### 2.3 Agent Response Component

Create `AgentResponseComponent` for streaming agent responses inline:

- Uses the existing `MarkdownRenderer::beginStream()/feedToken()/endStream()`
- Styled left bar (purple/magenta) matching the input component
- Shows a `Spinner` with status label during generation
- Tool calls rendered as labeled sections (tool name, arguments summary)
- Tool results collapsed by default, expandable

### 2.4 Agent Session Integration

Integrate `AgentSession` into `Shell::run()`:

```
Shell::run()
  └── prompt loop
       ├── Action::Submit    → Shell::execute()  (existing)
       ├── Action::AgentMode → enter agent mode
       │    ├── swap PromptComponent → AgentInputComponent
       │    ├── on submit: AgentSession::processMessage()
       │    │    ├── stream response → AgentResponseComponent
       │    │    ├── tool calls → execute tools → feed results back
       │    │    └── loop until no more tool calls
       │    └── on Escape: restore PromptComponent
       └── (other actions unchanged)
```

### 2.5 Contour VT Extension — Last Command Output

Integrate the Contour terminal emulator's VT extension for reading last command output:

- Send `CSI ? 2040 n` to request the output of the most recent command (between OSC 133 markers)
- Terminal responds with the captured output
- Agent can reference this output without re-running the command
- Graceful fallback for non-Contour terminals (feature detection via DA response)

### 2.6 Image Input — Clipboard Paste (Ctrl+V)

Allow users to paste images from the clipboard into agent mode queries:

**New dependency:** Add `stb_image` (header-only) for decoding clipboard image data (PNG, JPEG).
Endo currently has no image decoding — `tui::ImageData` expects raw RGBA pixels.

**Implementation:**

1. **Clipboard image detection** — Extend `PasteEvent` (currently text-only) to detect image data.
   Terminals that support OSC 52 clipboard access or kitty's clipboard protocol can provide
   base64-encoded image data. For X11/Wayland, use `xclip -selection clipboard -t image/png -o`
   or `wl-paste --type image/png` as a fallback.
2. **Image decoding** — Decode the raw PNG/JPEG bytes into `tui::ImageData` (RGBA pixels) via
   `stb_image` for preview rendering.
3. **Thumbnail preview** — Show a downscaled sixel preview of the pasted image inline in the
   `AgentInputComponent`, below the text input. Use `tui::encodeSixel()` + `Canvas::drawImage()`
   with the existing cell pixel dimensions from `Terminal::cellPixelWidth()/cellPixelHeight()`.
4. **Attachment tracking** — Store the original image bytes (PNG/JPEG) as an `ImageBlock` attachment
   on the pending message. Multiple images can be attached to a single query.
5. **API serialization** — When the query is submitted, `ImageBlock` attachments are serialized
   per-provider (see Phase 1.2). Providers that don't support image input (`supportsImageInput()
   == false`) get a warning: "This provider does not support image input."
6. **Remove attachment** — `Ctrl+Shift+V` or a `/remove-image` command to detach the last image.

**Keyboard flow:**

```
Agent mode → Ctrl+V (image on clipboard)
  → decode image → show sixel thumbnail inline
  → user types question about the image
  → Enter → message sent with text + ImageBlock attachment
```

### 2.7 Image Output — Inline LLM Response Images

When the LLM returns image data in its response (see Phase 1.4), render it inline
in the primary screen using endo's existing sixel infrastructure:

**Implementation:**

1. **Provider normalization** — `GenerateResult::content` contains interleaved `TextBlock` and
   `ImageBlock` entries (see Phase 1.2). The `AgentResponseComponent` iterates the content blocks
   in order.
2. **Image decoding** — Decode `ImageBlock::data` (PNG/JPEG bytes) into `tui::ImageData` (RGBA
   pixels) via `stb_image`.
3. **Sixel rendering** — Encode the decoded image via `tui::encodeSixel()` and render inline using
   `Canvas::drawImage()`. Scale the image to fit the terminal width (respect column count and
   cell pixel dimensions).
4. **Aspect ratio preservation** — Calculate display dimensions from the image's native aspect ratio
   and the terminal's cell pixel dimensions. Cap at terminal width, scale height proportionally.
5. **Fallback for non-sixel terminals** — If the terminal doesn't support sixel (detected via DA
   response), show a placeholder: `[Image: 1024x768, 245 KB — sixel not supported]` with an
   option to save the image to disk (`/save-image <path>`).
6. **Streaming** — Images arrive as complete base64 blocks (not streamed token-by-token). The
   `AgentResponseComponent` buffers the current content block and renders it when complete.
   Text blocks before and after the image stream normally via `MarkdownRenderer`.

**Provider-specific notes:**

| Provider | How images arrive | Handling |
|----------|-------------------|----------|
| Gemini 2.0+/2.5 Flash | Native `inlineData` parts interleaved with text | Decode and render inline |
| OpenAI (Responses API) | `image_generation` tool result with base64 data | Extract from tool result, render inline |
| Claude | N/A (never returns images) | No special handling needed |

**Touches:** `src/shell/PromptComponent.hpp/cpp`, `src/shell/Prompt.hpp/cpp`, `src/shell/Shell.cpp`,
new `src/agent/AgentInputComponent.hpp/cpp`, `src/agent/AgentResponseComponent.hpp/cpp`,
`src/agent/AgentSession.hpp/cpp`, `src/tui/Sixel.hpp` (image scaling helper)

---

## Phase 3: Tool System — Shell-Native Tools

**Goal:** Provide a core set of built-in tools that the agent can call without any external MCP
server. Endo's unique advantage: `Shell::execute()` gives the agent direct shell access.

### 3.1 Tool Interface and Registry

```
src/agent/tools/
├── AgentTool.hpp             # Abstract tool interface
├── ToolRegistry.hpp/.cpp     # Registers and dispatches built-in tools
├── ReadFileTool.hpp/.cpp     # Read file contents (with line range)
├── WriteFileTool.hpp/.cpp    # Write/create files
├── EditFileTool.hpp/.cpp     # Exact string replacement
├── GlobTool.hpp/.cpp         # File pattern matching
├── GrepTool.hpp/.cpp         # Content search (regex)
├── ShellExecuteTool.hpp/.cpp # Shell command execution via Shell::execute()
└── GitTool.hpp/.cpp          # Git operations (status, diff, log, commit)
```

Each tool implements:

- `name() -> std::string_view` — tool name (e.g., `"shell_execute"`)
- `definition() -> ToolDefinition` — JSON Schema for `inputSchema`
- `execute(json arguments) -> std::expected<ToolResult, ToolError>` — run and return output
- `risk() -> ToolRisk` — risk classification (see Phase 4)

### 3.2 Shell Execute Tool

The `ShellExecuteTool` wraps `Shell::execute()` — endo's unique advantage over external agents:

- **Input:** `{ command: string, timeout_ms?: int }`
- **Behavior:** Execute via `Shell::execute(command)`. The command runs in the same shell
  environment (aliases, functions, environment variables, F# bindings all available).
- **Output:** Command output (captured via pipe), exit code
- **Timeout:** Default 120s, max 600s. Kill child process group on timeout.

This means the agent inherits endo's full capabilities: pipelines, F# expressions,
structured output recognition, job control — no separate `bash` process needed.

### 3.3 File Tools

Standard coding-assistant file operations:

| Tool | Input | Behavior |
|------|-------|----------|
| `read_file` | `{ path, offset?, limit? }` | Read with optional line range, return with line numbers |
| `write_file` | `{ path, content }` | Write content, create directories as needed |
| `edit_file` | `{ path, old_string, new_string, replace_all? }` | Exact string replacement, fail if not unique |
| `glob` | `{ pattern, path? }` | File pattern matching, sorted by mtime |
| `grep` | `{ pattern, path?, glob?, context? }` | Regex search across files with context lines |

### 3.4 Git Tool

Git operations with safety guardrails:

- **Input:** `{ subcommand: string, args?: [string] }`
- **Read operations** (auto-approved): `status`, `diff`, `log`, `branch`, `show`
- **Write operations** (require approval): `add`, `commit`, `checkout`, `merge`
- **Blocked by default:** `push --force`, `reset --hard`, `clean -f`

### 3.5 Integration with AgentSession

`AgentSession::executeToolCalls()` dispatches in order:

1. Check `ToolRegistry` for built-in tools
2. Fall back to `ServerManager` for MCP tools (Phase 6)
3. Return `ToolResult { isError = true }` for unknown tool names

**Touches:** new `src/agent/tools/` directory, `src/agent/AgentSession.hpp/cpp`, `CMakeLists.txt`

---

## Phase 4: Permission & Safety System

**Goal:** Classify tools by risk and gate dangerous operations on user approval.
Permission prompts render inline in the primary screen.

### 4.1 Risk Classification

```cpp
enum class ToolRisk {
    ReadOnly,    // read_file, glob, grep, git status/log/diff — auto-approved
    Mutating,    // write_file, edit_file, git add/commit — prompt once per session
    Destructive, // shell_execute (some commands), git push/reset --hard — always prompt
};
```

### 4.2 Permission Manager

```
src/agent/
└── PermissionManager.hpp/.cpp
```

- Per-session approval set: `std::set<std::string> approvedTools`
- `ReadOnly` tools: always auto-approved
- `Mutating` tools: prompt on first use, remember approval for the session
- `Destructive` tools: always prompt with command preview
- Permission prompts rendered inline (styled confirmation bar, `[y/n/a]`)

### 4.3 Shell Command Safety

Special handling for the `shell_execute` tool:

- Parse command string for known-dangerous patterns (`rm -rf /`, `git push --force`, `mkfs`, etc.)
- Elevate risk classification based on detected patterns
- Block interactive commands that need a TTY (`vim`, `less`, `top`, `git rebase -i`)
- Enforce timeout, kill child process group on expiry

### 4.4 Configurable Policy

In `~/.config/endo/agent.yml`:

```yaml
permissions:
  policy: ask              # ask | trust_session | trust_all | read_only
  trusted_tools:           # always auto-approve these specific tools
    - read_file
    - glob
    - grep
  blocked_patterns:        # always block these shell patterns
    - "rm -rf /"
    - ":(){ :|:& };:"
```

**Touches:** `src/agent/PermissionManager.hpp/cpp`, `src/agent/AgentSession.hpp/cpp`,
`src/agent/tools/ShellExecuteTool.hpp/cpp`

---

## Phase 5: Context Management

**Goal:** Keep conversations within the LLM's context window during long coding sessions.

### 5.1 Token Tracking

- Estimate token counts per message (provider-specific tokenizer or heuristic)
- Track cumulative usage in `ConversationHistory`
- Expose `ConversationHistory::estimatedTokenCount() -> size_t`
- Display token usage in the agent status bar

### 5.2 Conversation Compaction

When approaching the context limit (80% of `contextSize`):

1. **Summarize** — Ask the LLM to summarize the conversation so far
2. **Replace** — Replace old messages with a single system message containing the summary
3. **Preserve** — Keep system prompt, summary, and the last N messages + pending tool results

### 5.3 Tool Result Truncation

- Truncate large tool results before adding to conversation (default 30 KB)
- Add `[truncated — X bytes omitted]` marker
- Configurable via `agent.yml`: `max_tool_result_size: 30720`

### 5.4 Project Context

Build initial context from existing infrastructure:

- **Working directory** from `Shell`
- **Git status** from `GitModule` (branch, dirty files, recent commits)
- **Project structure** — condensed file tree (respecting `.gitignore`)
- **Rules files** — load `CLAUDE.md`, `AGENT.md`, or `.endo/agent-rules.md` from project root
- **Memory files** — persistent agent memory in `~/.config/endo/agent-memory/`

### 5.5 System Prompt Assembly

Compose the system prompt from:

1. Base agent instructions (built-in)
2. Global rules (`~/.config/endo/agent-rules/*.md`)
3. Project rules (`<project-root>/CLAUDE.md`, `<project-root>/AGENT.md`)
4. Project context (CWD, git status, file tree)
5. Memory files (learnings from past sessions)

**Touches:** `src/agent/ConversationHistory.hpp/cpp` (from mychat `ChatSession`),
`src/agent/AgentSession.hpp/cpp`, `src/agent/AgentConfig.hpp`

---

## Phase 6: MCP Support

**Goal:** Enable the agent to use external MCP (Model Context Protocol) servers alongside
built-in tools.

### 6.1 MCP Client — Stdio Transport

Bootstrap from mychat's `src/mcp/` directory:

```
src/agent/mcp/
├── Transport.hpp             # Abstract transport interface (from mychat)
├── StdioTransport.hpp/.cpp   # Stdio pipe transport (from mychat)
├── JsonRpc.hpp/.cpp          # JSON-RPC 2.0 helpers (from mychat)
├── McpClient.hpp/.cpp        # MCP protocol client (from mychat)
└── ServerManager.hpp/.cpp    # Multi-server routing + ToolRegistry integration
```

- Adapt `Result<T>` → `std::expected<T, McpError>`
- Adapt namespace `mychat` → `endo::agent::mcp`
- `ServerManager` aggregates both MCP tools and built-in `ToolRegistry` tools

### 6.2 Configuration

In `~/.config/endo/agent.yml`:

```yaml
mcp_servers:
  filesystem:
    command: npx
    args: ["-y", "@modelcontextprotocol/server-filesystem", "/home/user/projects"]

  github:
    command: npx
    args: ["-y", "@modelcontextprotocol/server-github"]
    env:
      GITHUB_TOKEN: "${GITHUB_TOKEN}"
```

### 6.3 HTTP/SSE Transport

Add `HttpTransport` using the existing `HttpClient`:

```
src/agent/mcp/
└── HttpTransport.hpp/.cpp    # Streamable HTTP transport (MCP over SSE)
```

- Implement MCP Streamable HTTP transport (POST for requests, SSE for notifications)
- Support session management via `Mcp-Session-Id` header
- Leverage existing `HttpClient::executeStreaming()` (from Phase 1.5)

### 6.4 Dynamic Tool Discovery

- Handle `notifications/tools/list_changed` from MCP servers
- Re-fetch tool list on notification
- Update `ServerManager` routing map dynamically
- Notify `AgentSession` of tool set changes

**Touches:** new `src/agent/mcp/` directory, `src/agent/AgentSession.hpp/cpp`,
`src/agent/AgentConfig.hpp`, `CMakeLists.txt`

---

## Phase 7: Agent TUI Enhancements

**Goal:** Polish the inline agent experience with visualization and navigation.

### 7.1 Slash Commands

Add a command system for common agent workflows:

| Command | Action |
|---------|--------|
| `/commit` | Stage changes, generate commit message, create commit |
| `/review` | Review staged changes or a PR |
| `/test` | Run project tests and analyze results |
| `/explain <file>` | Explain a file's purpose and structure |
| `/fix` | Analyze the last error and suggest a fix |

- Register commands in a `SlashCommandRegistry`
- Each command expands to a prompt + optional tool sequence
- Tab completion via existing `CompletionPopup` infrastructure

### 7.2 Tool Execution Visualization

- Show a status line per tool call: tool name, arguments summary, elapsed time
- Use existing `Spinner` with tool-specific labels during execution
- Display tool result summary (success/error, output size) after completion
- Diff rendering for `edit_file` results (green additions, red deletions)

### 7.3 Conversation History Navigation

- Scrollback through previous agent exchanges
- `Ctrl+Up`/`Ctrl+Down` to cycle through conversation turns
- Search through conversation history (`Ctrl+R` in agent mode)

### 7.4 Alt-Screen Fullscreen Agent View

Available on demand for focused agent interaction:

- `F11` or `/fullscreen` toggles alt-screen agent view
- Split layout: conversation pane + file preview pane
- Rich rendering using existing `Screen` (Fullscreen viewport mode)
- `Escape` or `q` returns to inline primary screen

**Touches:** `src/agent/AgentInputComponent.hpp/cpp`, `src/agent/AgentResponseComponent.hpp/cpp`,
new `src/agent/SlashCommandRegistry.hpp/cpp`, `src/tui/MarkdownRenderer.hpp/cpp`

---

## Phase 8: Advanced Agent Features

**Goal:** Evolve from a single-turn tool caller into a sophisticated coding agent.

### 8.1 Parallel Tool Execution

Upgrade `AgentSession::executeToolCalls()`:

- Identify independent tool calls (no shared state dependencies)
- Execute independent calls concurrently using `std::jthread` or `std::async`
- Collect results and return in original order
- Serial fallback for tools with side effects on shared state

### 8.2 Agent-Powered Shell Completion

Use the LLM to enhance shell completions:

- When standard completion yields no results, offer to query the agent
- Agent suggests commands based on natural language intent
- Integrate with existing `CompletionPopup` — agent suggestions labeled `[AI]`

### 8.3 Error Recovery Suggestions

When a shell command fails (non-zero exit code):

- Offer inline agent analysis: "Want me to explain this error? `[y/n]`"
- Agent reads the error output (via Contour VT extension or captured stderr)
- Suggests corrective commands or code fixes
- User can accept suggestions directly into the prompt

### 8.4 Plan Mode

Structured planning workflow:

- Agent enters plan mode: explores codebase, reads files, builds understanding
- Produces a numbered step-by-step implementation plan
- User reviews and approves (`[y]es / [n]o / [e]dit`)
- Agent executes the approved plan, tracking progress inline
- Status bar shows: `Step 3/7: Writing unit tests...`

### 8.5 Memory System

Persistent agent memory across sessions:

- Store key learnings, project conventions, user preferences in `~/.config/endo/agent-memory/`
- Auto-save when the agent discovers stable patterns
- Load memory into system prompt on session start
- Organize by project (project-level memory files)

**Touches:** `src/agent/AgentSession.hpp/cpp`, `src/agent/ConversationHistory.hpp/cpp`,
new `src/agent/PlanExecutor.hpp/cpp`, `src/shell/PromptComponent.hpp/cpp` (completion integration)

---

## Phase 9: Multi-Agent Teams

**Goal:** Enable the user to create a team of AI agents that collaborate on complex tasks.
Each agent has an assigned role, one agent serves as the team leader that supervises and
coordinates the others. Agents communicate via a structured message-passing system.

### 9.1 Team Data Model

```
src/agent/team/
├── Team.hpp/.cpp              # Team lifecycle, agent registry
├── TeamAgent.hpp/.cpp         # Individual agent within a team
├── TeamConfig.hpp             # Team/agent configuration data model
├── MessageBus.hpp/.cpp        # Cross-agent message passing
├── TaskBoard.hpp/.cpp         # Shared task tracking (create, assign, complete)
└── TeamRenderer.hpp/.cpp      # TUI rendering for team activity
```

**Core types:**

```cpp
/// Role assigned to an agent within a team.
struct AgentRole {
    std::string name;             // e.g., "researcher", "implementer", "tester", "reviewer"
    std::string systemPrompt;     // Role-specific instructions appended to the base system prompt
    std::vector<std::string> allowedTools;  // Tool whitelist (empty = all tools)
    ToolRisk maxRiskLevel = ToolRisk::Mutating;  // Cap on tool risk for this role
};

/// Configuration for a single agent within a team.
struct TeamAgentConfig {
    std::string id;               // Unique agent identifier (e.g., "researcher-1")
    AgentRole role;
    std::string provider;         // LLM provider override (optional, defaults to team-level)
    std::string model;            // Model override (optional)
};

/// Configuration for an entire team.
struct TeamConfig {
    std::string name;             // Team name (e.g., "feature-auth")
    std::string leaderId;         // Which agent is the team leader
    std::vector<TeamAgentConfig> agents;
    size_t maxConcurrentAgents = 4;
};
```

### 9.2 Team Leader Agent

The team leader is a distinguished agent responsible for:

- **Task decomposition** — Breaking the user's request into sub-tasks
- **Task assignment** — Assigning sub-tasks to teammate agents based on their roles
- **Progress monitoring** — Tracking task completion, detecting blockers
- **Result aggregation** — Collecting teammate outputs and synthesizing a final response
- **Conflict resolution** — When teammates produce contradictory results

The leader has an augmented system prompt with team-management instructions and access to
team-coordination tools (see 9.4). The leader's `AgentSession` receives teammate messages
as tool results, keeping the coordination within the standard agent loop.

### 9.3 Cross-Agent Communication — Message Bus

Agents communicate through a typed message bus (not direct function calls):

```cpp
/// Message passed between agents via the MessageBus.
struct AgentMessage {
    std::string senderId;          // Source agent ID
    std::string recipientId;       // Target agent ID (or "broadcast")
    MessageType type;              // See below
    std::string content;           // Message payload (text, JSON, etc.)
    std::chrono::steady_clock::time_point timestamp;
};

enum class MessageType {
    TaskAssignment,    // Leader → agent: here's your task
    TaskUpdate,        // Agent → leader: progress/status update
    TaskComplete,      // Agent → leader: task finished, here are results
    DirectMessage,     // Agent → agent: peer-to-peer communication
    Broadcast,         // Agent → all: team-wide announcement
    ShutdownRequest,   // Leader → agent: please finish and shut down
    ShutdownAck,       // Agent → leader: acknowledged, shutting down
};
```

**Message delivery:**

- The `MessageBus` is a thread-safe, bounded MPMC queue per agent
- Each `TeamAgent` polls its inbox between agent loop iterations
- Incoming messages are injected into the agent's `ConversationHistory` as system messages
- The leader sees all teammate messages (star topology); teammates can DM each other or broadcast

### 9.4 Team-Coordination Tools

The team leader gets additional built-in tools for team management:

| Tool | Input | Behavior |
|------|-------|----------|
| `team_assign_task` | `{ agent_id, task, context? }` | Send a task assignment to a specific agent |
| `team_broadcast` | `{ message }` | Send a message to all agents |
| `team_check_status` | `{ agent_id? }` | Query status of one or all agents |
| `team_shutdown` | `{ agent_id? }` | Request graceful shutdown of one or all agents |

All agents (including non-leaders) get:

| Tool | Input | Behavior |
|------|-------|----------|
| `team_send_message` | `{ recipient_id, message }` | Send a DM to another agent |
| `team_report_status` | `{ status, details }` | Report progress to the leader |
| `team_request_help` | `{ description }` | Ask the leader for guidance or reassignment |

These tools are registered in the `ToolRegistry` only when the agent is part of a team.

### 9.5 Shared Task Board

A lightweight task tracker shared across the team:

```cpp
struct TeamTask {
    std::string id;
    std::string title;
    std::string description;
    std::string assigneeId;        // Agent assigned to this task
    TaskStatus status;             // Pending, InProgress, Completed, Blocked
    std::vector<std::string> blockedBy;  // Task IDs this depends on
    std::string result;            // Output when completed
};
```

- The leader creates and assigns tasks via `team_assign_task`
- Agents update task status via `team_report_status`
- The task board is stored in memory (not persisted to disk) — ephemeral per team session
- The TUI can render task board state (see 9.7)

### 9.6 Agent Lifecycle

```
User: "Create a team to refactor the auth module"
  │
  └── Shell activates team mode
       ├── Parse team config (inline YAML or from file)
       ├── Create Team with leader + N agents
       ├── Each TeamAgent gets:
       │    ├── Own AgentSession (isolated ConversationHistory)
       │    ├── Own LlmProvider instance (or shared, thread-safe)
       │    ├── Shared ToolRegistry + ServerManager (thread-safe)
       │    ├── MessageBus inbox
       │    └── Role-specific system prompt
       │
       ├── Leader receives the user's request
       ├── Leader decomposes into sub-tasks → assigns via team tools
       ├── Agents work concurrently (std::jthread per agent)
       │    ├── Agent loop: generate → tool calls → execute → report
       │    └── Poll inbox between iterations for new messages
       ├── Leader monitors progress, reassigns on failure
       └── Leader synthesizes final result → present to user
```

**Team creation UX:**

- `/team create` slash command opens an inline team configuration prompt
- Pre-defined team templates for common workflows:

```yaml
# ~/.config/endo/team-templates/code-review.yml
name: code-review
leader: reviewer
agents:
  - id: reviewer
    role:
      name: reviewer
      system_prompt: "You are a senior code reviewer. Review changes for correctness, style, and security."
      max_risk: read_only
  - id: tester
    role:
      name: tester
      system_prompt: "You are a test engineer. Write and run tests for the changes under review."
      allowed_tools: [read_file, write_file, edit_file, shell_execute, grep, glob]
  - id: documenter
    role:
      name: documenter
      system_prompt: "You are a documentation writer. Update docs to reflect code changes."
      allowed_tools: [read_file, write_file, edit_file, glob]
```

- Ad-hoc team creation: `/team create researcher implementer tester` — auto-generates roles
  from built-in templates with sensible defaults

### 9.7 Team TUI Rendering

Team activity renders in the primary screen (inline-first principle):

- **Team status bar** — Persistent line showing: team name, agent count, active/idle status
  per agent, task progress (`3/7 tasks complete`)
- **Agent activity feed** — Interleaved, chronological view of agent actions (tool calls,
  messages, completions). Each agent's output is color-coded by role.
- **Leader responses** — The leader's synthesized responses render like normal agent responses
  (purple bar, streaming markdown)
- **Teammate activity** — Collapsed by default (one-line summary per action), expandable inline
- **Alt-screen team dashboard** — `F11` or `/team dashboard` opens a full-screen view:
  - Left pane: task board (status, assignee, progress)
  - Right pane: selected agent's conversation history
  - Bottom pane: message bus activity log

### 9.8 Configuration

In `~/.config/endo/agent.yml`:

```yaml
teams:
  max_concurrent_agents: 4        # Global cap across all teams
  default_provider: claude        # Default provider for team agents
  default_model: claude-sonnet-4-5-20250929
  template_dir: ~/.config/endo/team-templates/

  # Per-agent provider overrides (e.g., use cheaper model for research)
  role_overrides:
    researcher:
      provider: openai_compat
      model: qwen2.5-coder:32b
```

**Touches:** new `src/agent/team/` directory, `src/agent/AgentSession.hpp/cpp`,
`src/agent/tools/ToolRegistry.hpp/cpp` (team tools), `src/agent/AgentConfig.hpp`,
`src/agent/SlashCommandRegistry.hpp/cpp` (`/team` commands), `CMakeLists.txt`

---

## Dependency Graph

```
Phase 1 (LLM Providers + Multimodal)
   │
   ├──→ Phase 2 (Agent Mode UX + Image I/O) ──→ Phase 3 (Tool System) ──→ Phase 4 (Permissions)
   │         │                                          │
   │         │                                          └──→ Phase 8 (Advanced Features)
   │         │                                          │       ├── 8.1 Parallel Tools
   │         │                                          │       ├── 8.2 AI Completion
   │         │                                          │       ├── 8.3 Error Recovery
   │         │                                          │       ├── 8.4 Plan Mode
   │         │                                          │       └── 8.5 Memory System
   │         │                                          │
   │         │                                          └──→ Phase 9 (Multi-Agent Teams)
   │         │
   │         └──→ Phase 7 (TUI Enhancements)
   │
   ├──→ Phase 5 (Context Management)
   │
   └──→ Phase 6 (MCP Support)
```

- **Phase 1** is the prerequisite — LLM provider support (including multimodal content model) unblocks all agentic features.
- **Phase 2** (UX) should come next — establishes the interaction model, including image paste input (2.6) and image output rendering (2.7).
- **Phase 3** (Tools) and **Phase 5** (Context) can proceed in parallel after Phase 2.
- **Phase 4** (Permissions) depends on Phase 3 (tools must exist to classify).
- **Phase 6** (MCP) is independent after Phase 1 — mychat code provides a head start.
- **Phase 7** (TUI) depends on Phase 2 (agent UX must exist to enhance).
- **Phase 8** (Advanced) depends on Phases 3 and 4 being in place.
- **Phase 9** (Multi-Agent Teams) depends on Phase 3 (tools), Phase 4 (permissions — each agent needs risk caps), and Phase 5 (context management — each agent needs its own conversation). This is the capstone feature.

---

## Target Directory Structure

```
src/agent/
├── Types.hpp                       # Core types: ChatMessage, ContentBlock, ToolCall, etc.
├── LlmProvider.hpp                 # Abstract provider interface (multimodal)
├── ClaudeProvider.hpp/.cpp         # Anthropic Claude API (SSE streaming)
├── OpenAiProvider.hpp/.cpp         # OpenAI-compatible API (OpenAI, Ollama, vLLM, LM Studio)
├── GeminiProvider.hpp/.cpp         # Google Gemini API (SSE streaming, image output)
├── ProviderFactory.hpp/.cpp        # Multi-provider creation and runtime switching
├── AgentConfig.hpp/.cpp            # Configuration data model + YAML loading
├── AgentSession.hpp/.cpp           # Multi-step agent loop (from mychat AgentLoop)
├── ConversationHistory.hpp/.cpp    # Conversation management (from mychat ChatSession)
├── PermissionManager.hpp/.cpp      # Tool risk classification and approval
├── AgentInputComponent.hpp/.cpp    # Styled agent input (purple bar, image paste)
├── AgentResponseComponent.hpp/.cpp # Streaming response renderer (text + inline images)
├── SlashCommandRegistry.hpp/.cpp   # /commit, /review, /test, /team, etc.
├── tools/
│   ├── AgentTool.hpp               # Abstract tool interface
│   ├── ToolRegistry.hpp/.cpp       # Built-in tool registry
│   ├── ReadFileTool.hpp/.cpp
│   ├── WriteFileTool.hpp/.cpp
│   ├── EditFileTool.hpp/.cpp
│   ├── GlobTool.hpp/.cpp
│   ├── GrepTool.hpp/.cpp
│   ├── ShellExecuteTool.hpp/.cpp
│   └── GitTool.hpp/.cpp
├── team/
│   ├── Team.hpp/.cpp               # Team lifecycle, agent registry
│   ├── TeamAgent.hpp/.cpp          # Individual agent within a team
│   ├── TeamConfig.hpp              # Team/agent role configuration
│   ├── MessageBus.hpp/.cpp         # Cross-agent message passing (MPMC)
│   ├── TaskBoard.hpp/.cpp          # Shared task tracking
│   └── TeamRenderer.hpp/.cpp       # TUI rendering for team activity
└── mcp/
    ├── Transport.hpp               # Abstract MCP transport
    ├── StdioTransport.hpp/.cpp     # Stdio pipe transport
    ├── HttpTransport.hpp/.cpp      # HTTP/SSE transport
    ├── JsonRpc.hpp/.cpp            # JSON-RPC 2.0 helpers
    ├── McpClient.hpp/.cpp          # MCP protocol client
    └── ServerManager.hpp/.cpp      # Multi-server routing
```

---

## Non-Goals (Out of Scope)

- **Separate agent binary** — agent mode is integrated into the endo shell, not a standalone tool
- **GUI / web interface** — endo is a terminal application
- **Local model hosting** — use external providers (Ollama, vLLM) via OpenAI-compatible API
- **Training or fine-tuning** — use pre-trained models only
- **Multi-user / server mode** — single-user local shell

---

## Success Criteria

The roadmap is complete when a user can:

1. Press `#` in the endo shell to activate agent mode
2. Ask the agent to read, understand, and modify code — all inline
3. Paste an image from the clipboard (`Ctrl+V`) and ask the agent about it — with inline sixel preview
4. See LLM-generated images rendered inline in the terminal (Gemini, OpenAI)
5. See the agent plan changes, execute tools (including shell commands), and iterate
6. Review diffs, approve mutations, and commit results — without leaving the shell
7. Use MCP servers to extend the agent's capabilities
8. Work through long sessions without context window issues
9. Create a team of agents (`/team create`) with assigned roles and a leader, watch them collaborate on complex tasks, and receive a synthesized result
10. Return to normal shell mode with `Escape` at any time
