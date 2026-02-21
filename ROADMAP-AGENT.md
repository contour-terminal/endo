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
   vLLM, LM Studio), Gemini, and future local models through a single provider interface.
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

## Completed Work

All completed features live under `src/agent/` (160 source files, 47 test files, 470 test cases).

### Phase 1: LLM Provider Abstraction ✅

Provider-agnostic interface with streaming SSE, multimodal content model (text + image blocks),
tool call normalization, and thinking/reasoning mode support (Off/Normal/Extended).

- **Providers:** Claude (`ClaudeProvider`), OpenAI (`OpenAiProvider`), OpenAI-compat (Ollama, vLLM, LM Studio), Gemini (`GeminiProvider`)
- **Runtime switching:** `ProviderFactory` creates instances for all authenticated providers, `switchProvider()` at runtime
- **Model cycling:** `CycleModel` action in `AgentInputComponent` cycles through available models within a provider
- **Key files:** `src/agent/providers/`, `src/agent/Types.hpp`, `src/agent/AgentConfig.hpp`

### Phase 2.1–2.4: Agent Mode UX ✅

Inline agent experience activated by pressing `#` on an empty prompt. Rounded-chrome header
with provider/model info, configurable prompt indicator, streaming markdown responses with
spinner, and purple accent color palette.

- **Input:** `AgentInputComponent` — multiline input, slash command completion, @-file completion, history
- **Output:** `AgentResponseRenderer` — streaming markdown, tool call rendering, spinner
- **Session:** `AgentSession` with conversation history, tool loop (up to 25 iterations)
- **Shell integration:** `Shell::runAgentMode()` with lazy provider init, background context loading
- **Key files:** `src/agent/ui/`, `src/agent/session/`

### Phase 5: Tool System ✅

18 built-in tools with `ToolRegistry` dispatch and `AgentTool` interface.

| Tool | Description |
|:-----|:------------|
| `read_file` | Read file contents (with line range) |
| `write_file` | Write/create files |
| `edit_file` | Exact string replacement |
| `glob` | File pattern matching |
| `grep` | Regex content search |
| `search` | Unified glob+grep (modes: files, content, files_with_matches, count) |
| `shell_execute` | Shell command execution via `Shell::execute()` |
| `endo_execute` | Execute endo commands directly |
| `git` | Git operations with safety guardrails |
| `explore` | Isolated exploration sub-agent (see Phase 12C below) |
| `web_search` | Web search via API |
| `web_fetch` | Fetch URL → markdown (HTML→markdown conversion via `HtmlUtils`) |
| `ask_user` | Prompt the user for input during agent execution |
| `save_memory` | Persist learnings to agent memory files |
| `submit_plan` | Submit a structured plan for user approval (plan mode) |
| `list_directory` | List directory contents |

- **Key files:** `src/agent/tools/`

### Phase 7: Context Management ✅

Token tracking, LLM-driven conversation compaction at 80% context fill, tool result
truncation (default 30 KB), git-aware project file tree, project/global rules loading,
memory file loading, and structured system prompt assembly.

- **Key files:** `src/agent/conversation/`, `src/agent/context/`

### Phase 7b: Interactive Authentication ✅

CLI commands: `endo agent login/status/switch/logout`. Interactive provider selection,
browser-based API key page opening, hidden input, key validation via lightweight API call,
atomic config save, stored-key priority over env var fallback.

- **Key files:** `src/agent/auth/LoginCommand.hpp/cpp`, `src/agent/auth/TerminalInput.hpp/cpp`

### Phase 7c: OAuth 2.0 PKCE ✅

OAuth PKCE flow for Claude MAX/Pro/Teams/Enterprise. Self-contained SHA-256 + base64url,
localhost callback server, token exchange/refresh, credential store (`agent-oauth.yaml`, 0600).
Transparent token refresh on HTTP 401 in `ClaudeProvider::generate()`.

- **Key files:** `src/agent/auth/OAuthFlow.hpp/cpp`, `src/agent/auth/OAuthCallbackServer.hpp/cpp`

### Phase 8.1–8.2: MCP Stdio + Config ✅

MCP client with JSON-RPC 2.0, stdio pipe transport (`posix_spawnp`), multi-server routing,
and `McpToolAdapter` that bridges MCP tools into the `ToolRegistry`. Configured via
`add_mcp_server`/`set_mcp_env`/`remove_mcp_server` shell builtins in `init.endo`.

- **Key files:** `src/agent/mcp/`

### Phase 9.1: Slash Commands ✅

`SlashCommandRegistry` with built-in commands (`/help`, `/plan`, `/reset`, `/tools`, `/status`)
and `CallbackSlashCommand` for dynamic registration. Tab completion via `SlashCommandCompleter`.

- **Key files:** `src/agent/commands/`

### Phase 10.4: Plan Mode ✅

Structured planning workflow: read-only exploration phase (`SessionMode::PlanOnly` filters
out mutating tools), `submit_plan` pseudo-tool for structured plan output, `PlanExecutor`
for step-by-step execution with user approval. Plan data model in `Plan.hpp`.

- **Key files:** `src/agent/Plan.hpp`, `src/agent/session/PlanExecutor.hpp/cpp`, `src/agent/tools/SubmitPlanTool.hpp/cpp`

### Phase 10.5: Memory System ✅

Persistent agent memory via `SaveMemoryTool`. Memory files stored in `~/.config/endo/agent-memory/`,
loaded into system prompt on session start via `ProjectContextLoader`.

- **Key files:** `src/agent/tools/SaveMemoryTool.hpp/cpp`, `src/agent/context/ProjectContextLoader.hpp/cpp`

### Phase 12C: Explore Tool ✅

Isolated exploration sub-agent that runs an inner `AgentSession` with read-only tools.
Returns only a concise summary to the main conversation, preventing context pollution
from intermediate grep/read results. Configurable max turns.

- **Key files:** `src/agent/tools/ExploreTool.hpp/cpp`

### Additional Completed Features

| Feature | Description | Key Files |
|:--------|:------------|:----------|
| **Tracing** | JSONL trace files recording full agent I/O flow (session header, messages, tool calls, errors) | `src/agent/tracing/AgentTracer.hpp/cpp`, `TraceReplay.hpp/cpp` |
| **Thinking Mode** | Off/Normal/Extended per provider. Cycle via keybinding. Maps to Claude `budget_tokens`, OpenAI `reasoning_effort`, Gemini `thinkingBudget` | `Types.hpp` (`ThinkingMode`), each provider |
| **Web Search** | `web_search` tool for live web queries | `src/agent/tools/WebSearchTool.hpp/cpp` |
| **Web Fetch** | `web_fetch` tool: URL → markdown with HTML cleaning | `src/agent/tools/WebFetchTool.hpp/cpp`, `HtmlUtils.hpp/cpp` |
| **Conversation Persistence** | Atomic JSON save/load (`ConversationHistoryStore`). System messages excluded. `/reset` clears history. | `src/agent/conversation/ConversationHistoryStore.hpp/cpp` |
| **Gemini Provider** | Native Google Gemini API support (SSE streaming, image output) | `src/agent/providers/GeminiProvider.hpp/cpp` |
| **Agent Worker** | Background agent execution infrastructure | `src/agent/session/AgentWorker.hpp/cpp`, `AgentManager.hpp/cpp` |

---

## Priority 1: Quick Wins (Low effort, High impact)

### Token & Cost Display ✅

**Status:** Completed | **Effort:** Small

Show token usage and estimated cost after each agent turn. Token usage is extracted from
all three providers (Claude, OpenAI, Gemini) and displayed after each response with
per-turn and cumulative tracking. `/status` slash command shows full session statistics.

- Extract `input_tokens`, `output_tokens`, `cache_read_tokens` from provider responses ✅
- Display after each response: `100 in / 50 out (80 cached) ~$0.0012` ✅
- Cumulative session totals in `/status` command ✅
- Hardcoded per-provider cost models with cache discount support ✅

### Diff Preview for edit_file

**Status:** Not started | **Effort:** Small

Show a colored diff preview before applying `edit_file` mutations. The tool already has
`old_string` and `new_string` — render a unified diff inline (green additions, red deletions)
before writing the file.

- Generate unified diff from `old_string`/`new_string`
- Render in `AgentResponseRenderer` with syntax highlighting for the file type
- Syntax highlighting we want to support in the beginning are: C++, CMake, Python, Endo, bash/sh, Markdown, JSON, YAML, git diff.
- Optional approval prompt for large edits (at least prepare the infrastructure for this, even if we don't enforce it yet)

### @-file Context Injection

**Status:** Done | **Effort:** Small

Allow `@path/to/file` in agent queries to automatically inject file contents into the
message context. The `FilePathCompleter` provides path completion; `FileReferenceExpander`
reads and attaches file contents when the message is submitted.

- [x] Parse `@path/to/file` references in submitted messages
- [x] Read file contents and append as XML-style `<file>` context blocks
- [x] Support `@path:N` and `@path:N-M` for line ranges
- [x] Truncate large files with `[truncated]` marker
- [x] Integration in `Shell::runAgentMode()` at all message submission paths

### Dynamic MCP Tool Discovery (Phase 8.4)

**Status:** Not started | **Effort:** Small

Handle `notifications/tools/list_changed` from MCP servers. Re-fetch tool list on
notification, update `ServerManager` routing map dynamically, notify `AgentSession`
of tool set changes.

### Session Resume Enhancement

**Status:** Partial (`ConversationHistoryStore` exists) | **Effort:** Small

The conversation persistence infrastructure exists but could be enhanced:

- Auto-save on each turn (not just on exit)
- Multiple named sessions (`/save-session  <name>`, `/load-session <name>`).
- Session list with timestamps and token counts
- Auto-resume last session on agent mode entry (configurable)
- On session load, replay the history for context (configurable).

### Model Switching at Runtime Enhancement

**Status:** Partial (`CycleModel`, `ProviderFactory::switchProvider()` exist) | **Effort:** Small

The model cycling infrastructure exists. Enhancements:

- `/model <name>` slash command for direct model selection
- `/model` without args shows interactive model picker
- Cross-provider switching (e.g., Claude → Gemini) within a session
- Display model change confirmation with capability diff

---

## Priority 2: Core Improvements (Medium effort, High impact)

### Permission & Safety System (Phase 6)

**Status:** Not started | **Effort:** Medium

Classify tools by risk and gate dangerous operations on user approval.
Permission prompts render inline in the primary screen.

#### 6.1 Risk Classification

```cpp
enum class ToolRisk {
    ReadOnly,    // read_file, glob, grep, git status/log/diff — auto-approved
    Mutating,    // write_file, edit_file, git add/commit — prompt once per session
    Destructive, // shell_execute (some commands), git push/reset --hard — always prompt
};
```

#### 6.2 Permission Manager

```
src/agent/
└── PermissionManager.hpp/.cpp
```

- Per-session approval set: `std::set<std::string> approvedTools`
- `ReadOnly` tools: always auto-approved
- `Mutating` tools: prompt on first use, remember approval for the session
- `Destructive` tools: always prompt with command preview
- Permission prompts rendered inline (styled confirmation bar, `[y/n/a]`)

#### 6.3 Shell Command Safety

Special handling for the `shell_execute` tool:

- Parse command string for known-dangerous patterns (`rm -rf /`, `git push --force`, `mkfs`, etc.)
- Elevate risk classification based on detected patterns
- Block interactive commands that need a TTY (`vim`, `less`, `top`, `git rebase -i`)
- Enforce timeout, kill child process group on expiry

#### 6.4 Configurable Policy

Configure in `init.endo`:

```sh
agent_permissions_policy <- "ask"          # ask | trust_session | trust_all | read_only
add_trusted_tool "read_file"
add_trusted_tool "glob"
add_trusted_tool "grep"
add_blocked_pattern "rm -rf /"
add_blocked_pattern ":(){ :|:& };:"
```

### Undo/Rollback System

**Status:** Not started | **Effort:** Medium

Track file modifications made by the agent and allow selective rollback. The agent
already operates through `edit_file`/`write_file` — intercept at the tool level to
record before-states.

- Snapshot file contents before each `write_file`/`edit_file` mutation
- `/undo` command to revert the last mutation (or last N)
- `/undo all` to revert all changes in the current session
- Git-based rollback when in a repository (stash/restore)
- Show diff of what would be reverted before applying

### Parallel Tool Execution (Phase 10.1)

**Status:** Not started | **Effort:** Medium

Upgrade `AgentSession::executeToolCalls()`:

- Identify independent tool calls (no shared state dependencies)
- Execute independent calls concurrently using `std::jthread` or `std::async`
- Collect results and return in original order
- Serial fallback for tools with side effects on shared state

### Prompt Caching

**Status:** Not started | **Effort:** Medium

Leverage provider-specific prompt caching to reduce costs and latency for long conversations.

- **Claude:** Use `cache_control` breakpoints on system prompt and tool definitions
- **OpenAI:** Automatic prompt caching (>= 1024 token prefix)
- **Gemini:** Context caching API for repeated prefixes
- Track cache hit/miss rates in token display
- Configure caching strategy via `init.endo` properties

### Image Input — Clipboard Paste (Phase 2.6)

**Status:** Not started | **Effort:** Medium

Allow users to paste images from the clipboard into agent mode queries:

- Extend `PasteEvent` to detect image data (OSC 52 or `xclip`/`wl-paste` fallback)
- Decode PNG/JPEG via `stb_image` (new header-only dependency) into `tui::ImageData`
- Show downscaled sixel preview inline in `AgentInputComponent`
- Store original bytes as `ImageBlock` attachment, serialize per-provider on submit
- Multiple images per query, `/remove-image` to detach

### Image Output — Inline LLM Response Images (Phase 2.7)

**Status:** Not started | **Effort:** Medium

Render LLM-generated images inline using existing sixel infrastructure:

- Decode `ImageBlock` from `GenerateResult::content` (Gemini native, OpenAI tool result)
- Render via `tui::encodeSixel()` + `Canvas::drawImage()`, scaled to terminal width
- Fallback for non-sixel terminals: `[Image: 1024x768, 245 KB]` + `/save-image <path>`

### Batch/Headless Agent Mode

**Status:** Not started | **Effort:** Medium

Run agent tasks non-interactively for CI/CD and scripting:

- `endo agent run "prompt"` — single-shot execution, output to stdout
- `endo agent run --file tasks.md` — execute tasks from a file
- `--json` flag for structured JSON output (tool calls, results, final answer)
- `--max-turns N` to cap tool loop iterations
- `--auto-approve` to skip permission prompts (for trusted pipelines)
- Exit code reflects agent success/failure

### Structured Output / JSON Mode

**Status:** Not started | **Effort:** Medium

Request structured JSON output from the agent:

- `--json-schema <schema>` flag for constrained output
- Provider-specific implementation: Claude `tool_use` extraction, OpenAI `response_format`,
  Gemini `responseMimeType`
- Useful for scripting: `endo agent run --json-schema '{"type":"object",...}' "analyze this code"`

---

## Priority 3: Major Features (High effort, High impact)

### MCP HTTP/SSE Transport (Phase 8.3)

**Status:** Not started | **Effort:** High

Add `HttpTransport` implementing MCP Streamable HTTP transport:

```
src/agent/mcp/
└── HttpTransport.hpp/.cpp    # Streamable HTTP transport (MCP over SSE)
```

- POST for requests, SSE for notifications
- Session management via `Mcp-Session-Id` header
- Leverage existing `HttpClient::executeStreaming()` from SSE support

### AI-powered Shell Completion (Phase 10.2)

**Status:** Not started | **Effort:** High

Use the LLM to enhance shell completions:

- When standard completion yields no results, offer to query the agent
- Agent suggests commands based on natural language intent
- Integrate with existing `CompletionPopup` — agent suggestions labeled `[AI]`

### Error Recovery Suggestions (Phase 10.3)

**Status:** Not started | **Effort:** High

When a shell command fails (non-zero exit code):

- Offer inline agent analysis: "Want me to explain this error? `[y/n]`"
- Agent reads the error output (via Contour VT extension or captured stderr)
- Suggests corrective commands or code fixes
- User can accept suggestions directly into the prompt

### Tool Execution Visualization (Phase 9.2)

**Status:** Not started | **Effort:** Medium

- Show a status line per tool call: tool name, arguments summary, elapsed time
- Use existing `Spinner` with tool-specific labels during execution
- Display tool result summary (success/error, output size) after completion
- Diff rendering for `edit_file` results (green additions, red deletions)

### Conversation History Navigation (Phase 9.3)

**Status:** Not started | **Effort:** Medium

- Scrollback through previous agent exchanges
- `Ctrl+Up`/`Ctrl+Down` to cycle through conversation turns
- Search through conversation history (`Ctrl+R` in agent mode)

### Code Review Tool

**Status:** Not started | **Effort:** High

A `/review` slash command that analyzes code changes:

- Review staged git changes or a specific PR
- Analyze for: correctness, style, security, performance, test coverage
- Output structured review with severity levels (critical, warning, suggestion)
- Optionally apply suggested fixes via `edit_file`
- Support for reviewing specific files or directories

### Agent Hooks / Event System

**Status:** Not started | **Effort:** High

Allow users to define custom hooks triggered by agent events:

- Hook points: `before_tool_call`, `after_tool_call`, `before_edit`, `after_response`
- Configure via `init.endo` builtins:

```sh
add_agent_hook "after_edit" "clang-format -i {file}"
add_agent_hook "after_response" "notify-send 'Agent done'"
```

- Run shell commands or endo scripts on events
- Use cases: auto-format after edits, auto-test after code changes, notifications

### Provider-Specific Prompt Optimization

**Status:** Not started | **Effort:** Medium

Optimize system prompts per provider to maximize each model's strengths:

- Claude: leverage XML tags, `<artifacts>`, thinking prefill
- OpenAI: structured system messages, function calling best practices
- Gemini: grounding, safety settings, structured prompts
- Provider-specific tool description formatting
- A/B testing framework for prompt variants

### Configuration Profiles

**Status:** Not started | **Effort:** Medium

Named configuration profiles for different workflows:

- `endo agent --profile coding` — high-capability model, all tools, auto-approve reads
- `endo agent --profile review` — read-only tools, code review focus
- `endo agent --profile cheap` — smaller/faster model, minimal tool set
- Profiles stored in `~/.config/endo/agent-profiles/`
- `/profile <name>` slash command to switch mid-session

### Contour VT Extension — Last Command Output (Phase 2.5)

**Status:** Not started | **Effort:** Medium

Integrate the Contour terminal emulator's VT extension for reading last command output:

- Send `CSI ? 2040 n` to request output of the most recent command (between OSC 133 markers)
- Agent can reference this output without re-running the command
- Graceful fallback for non-Contour terminals (feature detection via DA response)

---

## Priority 4: Strategic / Long-term

### Local Model Inference — llama.cpp (Phase 3)

**Status:** Not started | **Effort:** Very high

Enable fully offline, privacy-preserving LLM inference by integrating llama.cpp as a
local model backend. Adds a `LlamaCppProvider` implementing the `LlmProvider` interface.

#### 3.1 llama.cpp Integration

```
src/agent/
├── LlamaCppProvider.hpp/.cpp   # Local inference via llama.cpp
└── ModelManager.hpp/.cpp       # GGUF model discovery, loading, memory management
```

**LlamaCppProvider** implements `LlmProvider`:

- `generate()` — Run inference with streaming token callback
- `supportsToolUse()` — `true` for tool-use fine-tuned models (Llama 3.x, Qwen 2.5, Mistral)
- `supportsImageInput()` — `true` for multimodal models (LLaVA, Llama 3.2-Vision)
- `contextSize()` — From GGUF metadata or config override

#### 3.2 Model Management

`ModelManager` handles model lifecycle:

- **Discovery** — Scan configured directories for `.gguf` files with metadata display
- **Loading** — Configurable context size, batch size, thread count, GPU layer count
- **Memory management** — Track VRAM/RAM usage, enforce limits
- **Hot-swap** — Unload/load models on `/model` command without restarting session

#### 3.3 GPU Acceleration

| Backend | Platform | Detection |
|---------|----------|-----------|
| CUDA | Linux, Windows (NVIDIA) | `libcuda.so` / CUDA toolkit |
| Vulkan | Linux, Windows (any GPU) | Vulkan driver |
| Metal | macOS (Apple Silicon) | Always available |
| CPU | All platforms | Fallback |

#### 3.4 Streaming and KV Cache

- Token-by-token generation via `StreamCallback` (same interface as API providers)
- Persistent KV cache across turns for fast multi-turn conversation
- Context shifting when KV cache fills (llama.cpp built-in)
- Batch inference for faster prefill

#### 3.5 Tool Call Parsing

- Grammar-constrained generation (GBNF) for valid JSON tool call output
- Model-specific chat templates (ChatML, Llama 3, Mistral) with tool-use system prompts
- Best-effort JSON parsing fallback

#### 3.6 Configuration

Configure in `init.endo`:

```sh
agent_provider <- "local"
agent_local_model_path <- "~/.local/share/endo/models/qwen2.5-coder-32b-q4_k_m.gguf"
agent_local_model_dir <- "~/.local/share/endo/models/"
agent_local_gpu_layers <- -1
agent_local_context_size <- 32768
agent_local_threads <- 8
agent_local_batch_size <- 512
agent_local_temperature <- 0.7
agent_local_flash_attention <- true
```

### Voice Input — whisper.cpp + VAD (Phase 4)

**Status:** Not started | **Effort:** Very high

Voice-to-text input using whisper.cpp for speech recognition and VAD for automatic
speech segmentation.

#### 4.1 whisper.cpp Integration

```
src/agent/voice/
├── WhisperEngine.hpp/.cpp          # whisper.cpp wrapper for speech-to-text
├── AudioCapture.hpp/.cpp           # Microphone input (PulseAudio/ALSA/CoreAudio/WASAPI)
├── VoiceActivityDetector.hpp/.cpp  # VAD for automatic speech segmentation
└── VoiceInputManager.hpp/.cpp      # Coordinates capture → VAD → transcription → input
```

#### 4.2 Audio Capture

| Platform | Backend | Library |
|----------|---------|---------|
| Linux | PulseAudio / PipeWire | libpulse |
| Linux (fallback) | ALSA | libasound |
| macOS | CoreAudio | AudioToolbox framework |
| Windows | WASAPI | Windows SDK |

#### 4.3 Voice Activity Detection

- **Silero VAD** — Lightweight ONNX-based model (~2 MB)
- **Energy-based fallback** — RMS energy threshold
- Configurable speech probability threshold, silence duration, minimum speech duration

#### 4.4 Input Modes

| Mode | Activation | Behavior |
|------|-----------|----------|
| Push-to-talk | Hold `Ctrl+Space` | Record while held, transcribe on release |
| Hands-free | Toggle `Ctrl+Shift+Space` | VAD-driven auto-detection |

#### 4.5 Configuration

Configure in `init.endo`:

```sh
agent_voice_enabled <- true
agent_voice_model_path <- "~/.local/share/endo/models/ggml-large-v3-turbo.bin"
agent_voice_language <- "auto"
agent_voice_vad_backend <- "silero"
agent_voice_vad_speech_threshold <- 0.5
agent_voice_vad_silence_duration_ms <- 800
agent_voice_input_mode <- "push_to_talk"
```

### Alt-Screen Fullscreen Agent View (Phase 9.4)

**Status:** Not started | **Effort:** High

Available on demand for focused agent interaction:

- `F11` or `/fullscreen` toggles alt-screen agent view
- Split layout: conversation pane + file preview pane
- Rich rendering using existing `Screen` (Fullscreen viewport mode)
- `Escape` or `q` returns to inline primary screen

### Multi-Agent Teams (Phase 11)

**Status:** Not started | **Effort:** Very high

Enable the user to create a team of AI agents that collaborate on complex tasks.

#### 11.1 Team Data Model

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
struct AgentRole {
    std::string name;             // e.g., "researcher", "implementer", "tester"
    std::string systemPrompt;     // Role-specific instructions
    std::vector<std::string> allowedTools;
    ToolRisk maxRiskLevel = ToolRisk::Mutating;
};

struct TeamConfig {
    std::string name;
    std::string leaderId;
    std::vector<TeamAgentConfig> agents;
    size_t maxConcurrentAgents = 4;
};
```

#### 11.2 Team Leader Agent

Responsible for task decomposition, assignment, progress monitoring, result aggregation,
and conflict resolution. Augmented system prompt with team-management instructions.
Leader's `AgentSession` receives teammate messages as tool results.

#### 11.3 Cross-Agent Communication — Message Bus

Thread-safe, bounded MPMC queue per agent. Messages injected into agent's
`ConversationHistory` as system messages. Star topology (leader sees all).

#### 11.4 Team-Coordination Tools

Leader tools: `team_assign_task`, `team_broadcast`, `team_check_status`, `team_shutdown`.
All agents: `team_send_message`, `team_report_status`, `team_request_help`.

#### 11.5 Shared Task Board

Lightweight in-memory task tracker: create, assign, complete, block/unblock.

#### 11.6 Agent Lifecycle

Each `TeamAgent` gets its own `AgentSession`, `LlmProvider` (via `ProviderFactory::createProvider()`),
shared `ToolRegistry`, `MessageBus` inbox, and role-specific system prompt.
Agents work concurrently via `std::jthread`.

#### 11.7 Team TUI Rendering

Inline team status bar, agent activity feed (color-coded by role), leader responses
as normal agent output, collapsed teammate activity. Alt-screen dashboard via `/team dashboard`.

#### 11.8 Configuration

Configure in `init.endo`:

```sh
agent_teams_max_concurrent <- 4
agent_teams_default_provider <- "claude"
agent_teams_template_dir <- "~/.config/endo/team-templates/"
set_team_role_provider "researcher" "openai_compat"
set_team_role_model "researcher" "qwen2.5-coder:32b"
```

### Codebase Intelligence (Phase 12A/B/D/E)

**Status:** Not started | **Effort:** Very high (phased)

Symbol-level codebase navigation, LSP integration, indexing, and semantic search.

#### 12A — Tree-sitter Symbol Extraction

```
src/agent/intelligence/
├── TreeSitterParser.hpp/.cpp       # tree-sitter wrapper
├── SymbolIndex.hpp/.cpp            # In-memory symbol table
├── SymbolExtractor.hpp/.cpp        # Language-specific AST queries
└── grammars/                       # Bundled grammars (C/C++, Python, JS/TS, Rust, Go)
```

New tools: `find_symbol`, `list_symbols`, `file_outline`.

#### 12B — LSP Client Integration (Optional)

Reuse `JsonRpc.hpp` from MCP for JSON-RPC 2.0 transport. Auto-discover LSP servers.
New tools: `goto_definition`, `find_references`, `workspace_symbols`.

#### 12D — Codebase Indexing & Caching

Background indexer with `inotify`/`kqueue` file watching, persistent index in
`~/.cache/endo/index/<project-hash>/`. Warm start on subsequent sessions.

#### 12E — Semantic / Embedding Search (Optional)

Local embedding model (via llama.cpp) for conceptual queries.
Tree-sitter-based chunking at function/class boundaries. HNSW vector index.
New tool: `semantic_search`.

#### 12.6 Configuration

Configure in `init.endo`:

```sh
agent_treesitter_enabled <- true
agent_treesitter_languages <- "auto"
agent_treesitter_max_file_size_kb <- 512
agent_lsp_enabled <- false
set_lsp_server "cpp" "clangd"
set_lsp_server "python" "pylsp"
agent_indexing_enabled <- true
agent_indexing_background <- true
agent_indexing_cache_dir <- "~/.cache/endo/index/"
add_index_exclude "build/"
add_index_exclude "node_modules/"
add_index_exclude ".git/"
agent_semantic_enabled <- false
agent_semantic_model_path <- "~/.local/share/endo/models/nomic-embed-code-q8_0.gguf"
```

**Recommended implementation order:** 12A → 12D → 12B → 12E

---

## Dependency Graph

```
Completed Foundation (Phases 1, 2, 5, 7, 7b, 7c, 8.1-8.2, 9.1, 10.4, 10.5, 12C)
   │
   ├── Priority 1: Quick Wins (no dependencies on each other)
   │    ├── Token/Cost Display
   │    ├── Diff Preview for edit_file
   │    ├── @-file Context Injection
   │    ├── Dynamic MCP Tool Discovery
   │    ├── Session Resume Enhancement
   │    └── Model Switching Enhancement
   │
   ├── Priority 2: Core Improvements
   │    ├── Permission & Safety System (Phase 6)
   │    ├── Undo/Rollback System
   │    ├── Parallel Tool Execution
   │    ├── Prompt Caching
   │    ├── Image Input (Phase 2.6)
   │    ├── Image Output (Phase 2.7) ── depends on 2.6 (shared stb_image)
   │    ├── Batch/Headless Agent Mode
   │    └── Structured Output / JSON Mode
   │
   ├── Priority 3: Major Features
   │    ├── MCP HTTP/SSE Transport (Phase 8.3)
   │    ├── AI Shell Completion (Phase 10.2)
   │    ├── Error Recovery (Phase 10.3)
   │    ├── Tool Execution Visualization (Phase 9.2)
   │    ├── Conversation Navigation (Phase 9.3)
   │    ├── Code Review Tool
   │    ├── Agent Hooks / Event System
   │    ├── Provider-Specific Prompt Optimization
   │    ├── Configuration Profiles
   │    └── Contour VT Extension (Phase 2.5)
   │
   └── Priority 4: Strategic
        ├── Local Models — llama.cpp (Phase 3) ── independent
        ├── Voice Input — whisper.cpp (Phase 4) ── independent, benefits from Phase 3 GPU
        ├── Alt-Screen Agent View (Phase 9.4)
        ├── Multi-Agent Teams (Phase 11) ── depends on Phase 6 (permissions)
        └── Codebase Intelligence (Phase 12)
             ├── 12A Tree-sitter ── independent
             ├── 12D Indexing ── depends on 12A
             ├── 12B LSP Client ── reuses MCP JSON-RPC
             └── 12E Semantic Search ── depends on 12D + Phase 3
```

---

## Current Directory Structure

```
src/agent/
├── AgentConfig.hpp/.cpp            # Configuration data model (agent-keys.yml loading + runtime property binding)
├── Plan.hpp                        # Plan data model (PlanStep, Plan, PlanStepStatus)
├── Types.hpp                       # Core types: ChatMessage, ContentBlock, ToolCall, etc.
├── auth/
│   ├── LoginCommand.hpp/.cpp       # endo agent login/status/switch/logout
│   ├── OAuthCallbackServer.hpp/.cpp # Localhost HTTP callback server
│   ├── OAuthFlow.hpp/.cpp          # OAuth PKCE flow, token exchange/refresh
│   └── TerminalInput.hpp/.cpp      # Hidden input, browser opening
├── commands/
│   ├── AgentHistoryProvider.hpp/.cpp # Input history for agent mode
│   ├── FilePathCompleter.hpp/.cpp  # @-file path completion
│   ├── SlashCommand.hpp            # Abstract slash command interface
│   ├── SlashCommandCompleter.hpp/.cpp # Tab completion for /commands
│   ├── SlashCommandRegistry.hpp/.cpp  # Command registration and dispatch
│   └── SlashCommands.hpp/.cpp      # Built-in commands (/help, /plan) + CallbackSlashCommand
├── context/
│   ├── ProjectContextLoader.hpp/.cpp  # Rules, memory, global rules loading
│   ├── ProjectFileTree.hpp/.cpp    # Git-aware condensed file tree
│   └── SystemPromptBuilder.hpp/.cpp # Structured system prompt assembly
├── conversation/
│   ├── ConversationCompactor.hpp/.cpp # LLM-driven summarization at 80% fill
│   ├── ConversationHistory.hpp/.cpp   # Message management with token tracking
│   ├── ConversationHistoryStore.hpp/.cpp # Atomic JSON persistence
│   └── TokenEstimator.hpp/.cpp     # Character-based heuristic token counting
├── mcp/
│   ├── JsonRpc.hpp/.cpp            # JSON-RPC 2.0 helpers
│   ├── McpClient.hpp/.cpp          # MCP protocol client
│   ├── McpError.hpp                # Error types
│   ├── McpToolAdapter.hpp/.cpp     # MCP → AgentTool bridge
│   ├── ServerManager.hpp/.cpp      # Multi-server routing
│   ├── StdioTransport.hpp/.cpp     # Stdio pipe transport
│   └── Transport.hpp               # Abstract transport interface
├── providers/
│   ├── ClaudeProvider.hpp/.cpp     # Anthropic Claude API (SSE, OAuth, thinking)
│   ├── GeminiProvider.hpp/.cpp     # Google Gemini API (SSE, image output)
│   ├── LlmProvider.hpp            # Abstract provider interface
│   ├── OpenAiProvider.hpp/.cpp     # OpenAI-compatible API
│   ├── ProviderFactory.hpp/.cpp    # Multi-provider creation and switching
│   └── ProviderModels.hpp          # Model capability definitions
├── session/
│   ├── AgentManager.hpp/.cpp       # Agent lifecycle management
│   ├── AgentMessages.hpp           # Message types for agent communication
│   ├── AgentSession.hpp/.cpp       # Multi-step agent loop (tool dispatch, compaction)
│   ├── AgentWorker.hpp/.cpp        # Background agent execution
│   └── PlanExecutor.hpp/.cpp       # Step-by-step plan execution
├── tools/
│   ├── AgentTool.hpp               # Abstract tool interface
│   ├── AskUserTool.hpp/.cpp        # Prompt user for input
│   ├── EditFileTool.hpp/.cpp       # Exact string replacement
│   ├── EndoExecuteTool.hpp/.cpp    # Endo shell command execution
│   ├── ExploreTool.hpp/.cpp        # Isolated exploration sub-agent
│   ├── GitTool.hpp/.cpp            # Git operations with safety guardrails
│   ├── GlobTool.hpp/.cpp           # File pattern matching
│   ├── GrepTool.hpp/.cpp           # Regex content search
│   ├── HtmlUtils.hpp/.cpp          # HTML → markdown conversion
│   ├── ListDirectoryTool.hpp/.cpp  # Directory listing
│   ├── ReadFileTool.hpp/.cpp       # File reading with line ranges
│   ├── SaveMemoryTool.hpp/.cpp     # Persist agent learnings
│   ├── SearchTool.hpp/.cpp         # Unified search (glob + grep modes)
│   ├── ShellExecuteTool.hpp/.cpp   # Shell command execution
│   ├── SubmitPlanTool.hpp/.cpp     # Plan submission for approval
│   ├── ToolRegistry.hpp/.cpp       # Tool registration and dispatch
│   ├── WebFetchTool.hpp/.cpp       # URL → markdown fetching
│   ├── WebSearchTool.hpp/.cpp      # Web search
│   └── WriteFileTool.hpp/.cpp      # File writing/creation
├── tracing/
│   ├── AgentTracer.hpp/.cpp        # JSONL trace file writer
│   └── TraceReplay.hpp/.cpp        # Trace replay for debugging
└── ui/
    ├── AgentInputComponent.hpp/.cpp # Styled agent input with completion
    └── AgentResponseRenderer.hpp/.cpp # Streaming markdown response renderer
```

---

## Documentation (**COMPLETE**)

Agent documentation in the mkdocs site under `docs/agent/`:

- **Overview** (`agent/index.md`) — What the agent is, providers, quick start, CLI commands
- **Configuration** (`agent/configuration.md`) — `init.endo` properties, `agent-keys.yml` key storage, MCP server setup, web search config
- **Tools & Commands** (`agent/tools.md`) — Built-in tools table, slash commands, plan mode

---

## Non-Goals (Out of Scope)

- **Separate agent binary** — agent mode is integrated into the endo shell, not a standalone tool
- **GUI / web interface** — endo is a terminal application
- **Training or fine-tuning** — use pre-trained models only
- **Multi-user / server mode** — single-user local shell
- **Speech synthesis / TTS** — voice output is out of scope; voice input (STT) via whisper.cpp is in scope

---

## Success Criteria

The roadmap is complete when a user can:

1. **Today:** Press `#` to activate agent mode, ask the agent to read/understand/modify code inline ✅
2. **Today:** Use multiple LLM providers (Claude, OpenAI, Gemini) with runtime switching ✅
3. **Today:** Use MCP servers to extend agent capabilities ✅
4. **Today:** Have the agent plan changes before executing them ✅
5. **Today:** Work through long sessions without context window issues ✅
6. **Today:** Authenticate via OAuth (Claude MAX/Pro) or API keys ✅
7. **Today:** Use web search and fetch within agent conversations ✅
8. See token usage and cost estimates after each turn ✅
9. Paste an image from the clipboard and ask the agent about it — with inline sixel preview
10. See LLM-generated images rendered inline (Gemini, OpenAI)
11. Run a local LLM via llama.cpp (`provider: local`) — fully offline, no API key required
12. Dictate a query via voice (`Ctrl+Space` push-to-talk) with real-time transcription
13. Review diffs, approve mutations, and commit results — without leaving the shell
14. Create a team of agents with assigned roles, watch them collaborate on complex tasks
15. Navigate symbols, outline files, and search code semantically without flooding the conversation
16. Run agent tasks in batch/headless mode for CI/CD integration
