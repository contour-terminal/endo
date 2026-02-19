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

## Phase 2: Agent Mode UX — Inline Experience (**2.1–2.4 COMPLETE**)

**Goal:** Provide a seamless inline agent experience activated from the shell prompt.
This is the core UX — the user stays in their terminal, responses stream inline.

**Status (2.1–2.4):** Fully implemented. Press `#` on an empty prompt to enter agent mode.
AgentInputComponent renders a rounded-chrome header line showing "agent" label with active
provider/model info (e.g., `╭─ agent │ claude/claude-sonnet-4-5-20250929 │ main`), followed by
input with configurable prompt indicator (default: `❯`, configurable via `prompt_indicator` in
`agent.yml`). AgentResponseRenderer (streaming markdown with spinner), AgentSession
(conversation history + LLM generation), SystemPromptBuilder (environment context), and
ConversationHistory are all in `src/agent/`. AgentColorPalette added to Theme.
Shell integration via `Shell::runAgentMode()` with lazy provider initialization.
**Instant prompt display:** Heavy context loading (project file scanning, git subprocess queries)
runs in a background thread via `std::async`. The prompt appears immediately on mode switch;
git branch appears in the header as a fragment update when the background thread completes
(~1-2s). Early submit before context is ready blocks briefly until the future resolves.
Bug fixes applied: prompt cleared on Ctrl+T entry (no lingering shell prompt),
HTTP error bodies captured and included in API error diagnostics, CPR/CellSizeReport
events filtered in `Terminal::poll()` to prevent escape sequence leaking into input.
Phases 2.5–2.7 (VT extension, image I/O) deferred.

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

- **Rounded-chrome header line** (`╭─ agent │ provider/model`) in agent purple accent color
- **Configurable prompt indicator** (default: `❯`, set via `prompt_indicator` in `agent.yml`)
- **Provider/model display** — header shows active provider and model name from `LlmProvider::modelInfo()`
- **Purple/magenta left bar** (`╰─` first line, `│` continuation) for visual distinction
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

## Phase 3: Local Model Inference — llama.cpp

**Goal:** Enable fully offline, privacy-preserving LLM inference by integrating llama.cpp as a
local model backend. This adds a `LlamaCppProvider` implementing the `LlmProvider` interface
from Phase 1, allowing the agent to run without any external API dependency.

### 3.1 llama.cpp Integration

Link against llama.cpp as a library dependency (not a subprocess):

```
src/agent/
├── LlamaCppProvider.hpp/.cpp   # Local inference via llama.cpp
└── ModelManager.hpp/.cpp       # GGUF model discovery, loading, memory management
```

**LlamaCppProvider** implements the `LlmProvider` interface:

- `generate()` — Run inference on the local model with streaming token callback
- `supportsToolUse()` — `true` for models with tool-use fine-tuning (Llama 3.x, Qwen 2.5, Mistral, etc.)
- `supportsImageInput()` — `true` for multimodal models (LLaVA, Llama 3.2-Vision, etc.)
- `supportsImageOutput()` — `false` (no local image generation models supported initially)
- `contextSize()` — Read from GGUF model metadata or user config override
- `modelInfo()` — Extract model name, parameter count, quantization level from GGUF metadata

### 3.2 Model Management

`ModelManager` handles model lifecycle:

- **Discovery** — Scan configured model directories for `.gguf` files, display model list with
  size, quantization, and parameter count
- **Loading** — Load model and create llama context with configurable parameters (context size,
  batch size, thread count, GPU layer count)
- **Memory management** — Track VRAM/RAM usage, enforce memory limits, warn when a model
  exceeds available memory
- **Hot-swap** — Unload current model and load a different one on provider switch via `/model`
  slash command without restarting the agent session

### 3.3 GPU Acceleration

Support hardware acceleration backends:

| Backend | Platform | Detection |
|---------|----------|-----------|
| CUDA | Linux, Windows (NVIDIA) | Check for `libcuda.so` / CUDA toolkit |
| Vulkan | Linux, Windows (any GPU) | Check for Vulkan driver |
| Metal | macOS (Apple Silicon) | Always available on macOS |
| CPU | All platforms | Fallback — always available |

- Auto-detect available backends at CMake configure time
- Runtime selection: prefer GPU, fall back to CPU
- Configurable GPU layer offloading (`n_gpu_layers` in config, `-1` = all layers)

### 3.4 Streaming and KV Cache

- **Streaming** — Token-by-token generation via `StreamCallback`, same interface as API providers.
  The TUI renders tokens identically regardless of whether they come from an API or local inference.
- **KV cache** — Persistent KV cache across turns within a session for fast multi-turn conversation.
  Avoids re-processing the entire conversation history on each turn.
- **Context shifting** — When KV cache fills, shift out oldest tokens (llama.cpp built-in support)
- **Batch inference** — Use llama.cpp batch API for parallel prompt evaluation (faster prefill)

### 3.5 Tool Call Parsing

Local models don't have native tool-calling wire formats. `LlamaCppProvider` handles this:

- **Structured output** — Use llama.cpp grammar-constrained generation (GBNF) to enforce valid
  JSON tool call output
- **Prompt templates** — Model-specific chat templates (ChatML, Llama 3, Mistral, etc.) with
  tool-use system prompts that instruct the model to emit tool calls in a standardized JSON format
- **Parsing** — Extract tool calls from model output, normalize into `GenerateResult { text, vector<ToolCall> }`
- **Fallback** — If grammar enforcement is unavailable, parse best-effort JSON from free-form output

### 3.6 Configuration

In `~/.config/endo/agent.yml`:

```yaml
provider: local                   # Use local llama.cpp inference

local:
  model_path: ~/.local/share/endo/models/qwen2.5-coder-32b-q4_k_m.gguf
  model_dir: ~/.local/share/endo/models/   # Directory to scan for models
  n_gpu_layers: -1                # -1 = offload all layers to GPU
  context_size: 32768             # Context window override (0 = use model default)
  threads: 8                      # CPU threads for inference
  batch_size: 512                 # Prompt evaluation batch size
  temperature: 0.7
  top_p: 0.9
  flash_attention: true           # Enable flash attention if supported
```

- Model selection at runtime via `/model` slash command (list available models, switch instantly)
- Memory usage displayed in agent status bar (VRAM/RAM, KV cache fill level)

**Touches:** new `src/agent/LlamaCppProvider.hpp/cpp`, `src/agent/ModelManager.hpp/cpp`,
`CMakeLists.txt` (llama.cpp dependency), `src/agent/AgentConfig.hpp`

---

## Phase 4: Voice Input — whisper.cpp + VAD

**Goal:** Enable voice-to-text input for agent mode using whisper.cpp for speech recognition
and Voice Activity Detection (VAD) for automatic speech segmentation. Users can dictate
queries hands-free or via push-to-talk.

### 4.1 whisper.cpp Integration

Link against whisper.cpp as a library dependency:

```
src/agent/voice/
├── WhisperEngine.hpp/.cpp          # whisper.cpp wrapper for speech-to-text
├── AudioCapture.hpp/.cpp           # Microphone input (PulseAudio/ALSA/CoreAudio/WASAPI)
├── VoiceActivityDetector.hpp/.cpp  # VAD for automatic speech segmentation
└── VoiceInputManager.hpp/.cpp      # Coordinates capture → VAD → transcription → input
```

**WhisperEngine** wraps whisper.cpp:

- Load Whisper GGML models (tiny, base, small, medium, large-v3-turbo)
- Real-time transcription of audio segments
- Language auto-detection or explicit language selection
- GPU-accelerated inference (same backends as Phase 3)

### 4.2 Audio Capture

`AudioCapture` provides cross-platform microphone input:

| Platform | Backend | Library |
|----------|---------|---------|
| Linux | PulseAudio / PipeWire | libpulse |
| Linux (fallback) | ALSA | libasound |
| macOS | CoreAudio | AudioToolbox framework |
| Windows | WASAPI | Windows SDK |

- 16 kHz mono PCM capture (Whisper's expected input format)
- Ring buffer for continuous audio streaming
- Device enumeration and selection

### 4.3 Voice Activity Detection (VAD)

`VoiceActivityDetector` segments continuous audio into speech chunks:

- **Silero VAD** — Lightweight ONNX-based VAD model (~2 MB), high accuracy, low latency
- **Energy-based fallback** — Simple RMS energy threshold for systems without ONNX runtime
- **Parameters:** speech probability threshold, silence duration (end-of-utterance trigger),
  minimum speech duration (filter noise bursts)
- **Output:** Speech segments with start/end timestamps → fed to WhisperEngine for transcription

### 4.4 Input Modes

Two voice input modes, selectable by the user:

| Mode | Activation | Behavior |
|------|-----------|----------|
| Push-to-talk | Hold `Ctrl+Space` | Record while held, transcribe on release |
| Hands-free | Toggle `Ctrl+Shift+Space` | VAD-driven: auto-detect speech start/end, transcribe segments continuously |

- Voice input injects transcribed text into `AgentInputComponent` (or `PromptComponent` in shell mode)
- Real-time partial transcription displayed as greyed-out preview text
- Final transcription replaces preview with confirmed text
- `Enter` still required to submit (voice only fills the input field)

### 4.5 Inline Status Rendering

Voice input status renders inline below the prompt:

- **Recording indicator** — Pulsing `●` with `Spinner` animation and elapsed time
- **Audio level meter** — Simple bar showing microphone input level (helps diagnose muted mic)
- **Transcription preview** — Partial results shown in dim text, updated in real-time
- **VAD state** — In hands-free mode: `Listening...` / `Speaking...` / `Processing...`

### 4.6 Configuration

In `~/.config/endo/agent.yml`:

```yaml
voice:
  enabled: true
  model_path: ~/.local/share/endo/models/ggml-large-v3-turbo.bin
  language: auto                   # or "en", "de", "ja", etc.
  vad:
    backend: silero                # silero | energy
    speech_threshold: 0.5          # Silero VAD confidence threshold
    silence_duration_ms: 800       # Silence before end-of-utterance
    min_speech_duration_ms: 250    # Minimum speech segment length
  input_mode: push_to_talk         # push_to_talk | hands_free
  audio_device: default            # or specific device name
  gpu_acceleration: true           # Use GPU for Whisper inference
```

- Model selection: smaller models (tiny/base) for low-latency real-time preview, larger models
  (large-v3-turbo) for final transcription accuracy
- Per-session override via `/voice` slash command (toggle on/off, switch mode, change model)

**Touches:** new `src/agent/voice/` directory, `src/agent/AgentInputComponent.hpp/cpp`,
`src/shell/PromptComponent.hpp/cpp`, `CMakeLists.txt` (whisper.cpp + audio library dependencies),
`src/agent/AgentConfig.hpp`

---

## Phase 5: Tool System — Shell-Native Tools (**COMPLETE**)

**Goal:** Provide a core set of built-in tools that the agent can call without any external MCP
server. Endo's unique advantage: `Shell::execute()` gives the agent direct shell access.

**Status:** Fully implemented. 7 built-in tools (read_file, write_file, edit_file, glob, grep,
shell_execute, git) with ToolRegistry dispatch. AgentSession tool loop (up to 25 iterations)
with ToolUseBlock/ToolResultBlock round-trip. OpenAI provider updated for User-role tool results.
Shell integration registers all tools in `runAgentMode()`. 47 new test cases, 128 total agent
test cases (460 assertions), all passing.

### 5.1 Tool Interface and Registry

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
- `risk() -> ToolRisk` — risk classification (see Phase 6)

### 5.2 Shell Execute Tool

The `ShellExecuteTool` wraps `Shell::execute()` — endo's unique advantage over external agents:

- **Input:** `{ command: string, timeout_ms?: int }`
- **Behavior:** Execute via `Shell::execute(command)`. The command runs in the same shell
  environment (aliases, functions, environment variables, F# bindings all available).
- **Output:** Command output (captured via pipe), exit code
- **Timeout:** Default 120s, max 600s. Kill child process group on timeout.

This means the agent inherits endo's full capabilities: pipelines, F# expressions,
structured output recognition, job control — no separate `bash` process needed.

### 5.3 File Tools

Standard coding-assistant file operations:

| Tool | Input | Behavior |
|------|-------|----------|
| `read_file` | `{ path, offset?, limit? }` | Read with optional line range, return with line numbers |
| `write_file` | `{ path, content }` | Write content, create directories as needed |
| `edit_file` | `{ path, old_string, new_string, replace_all? }` | Exact string replacement, fail if not unique |
| `glob` | `{ pattern, path? }` | File pattern matching, sorted by mtime |
| `grep` | `{ pattern, path?, glob?, context? }` | Regex search across files with context lines |

### 5.4 Git Tool

Git operations with safety guardrails:

- **Input:** `{ subcommand: string, args?: [string] }`
- **Read operations** (auto-approved): `status`, `diff`, `log`, `branch`, `show`
- **Write operations** (require approval): `add`, `commit`, `checkout`, `merge`
- **Blocked by default:** `push --force`, `reset --hard`, `clean -f`

### 5.5 Integration with AgentSession

`AgentSession::executeToolCalls()` dispatches in order:

1. Check `ToolRegistry` for built-in tools
2. Fall back to `ServerManager` for MCP tools (Phase 8)
3. Return `ToolResult { isError = true }` for unknown tool names

**Touches:** new `src/agent/tools/` directory, `src/agent/AgentSession.hpp/cpp`, `CMakeLists.txt`

---

## Phase 6: Permission & Safety System

**Goal:** Classify tools by risk and gate dangerous operations on user approval.
Permission prompts render inline in the primary screen.

### 6.1 Risk Classification

```cpp
enum class ToolRisk {
    ReadOnly,    // read_file, glob, grep, git status/log/diff — auto-approved
    Mutating,    // write_file, edit_file, git add/commit — prompt once per session
    Destructive, // shell_execute (some commands), git push/reset --hard — always prompt
};
```

### 6.2 Permission Manager

```
src/agent/
└── PermissionManager.hpp/.cpp
```

- Per-session approval set: `std::set<std::string> approvedTools`
- `ReadOnly` tools: always auto-approved
- `Mutating` tools: prompt on first use, remember approval for the session
- `Destructive` tools: always prompt with command preview
- Permission prompts rendered inline (styled confirmation bar, `[y/n/a]`)

### 6.3 Shell Command Safety

Special handling for the `shell_execute` tool:

- Parse command string for known-dangerous patterns (`rm -rf /`, `git push --force`, `mkfs`, etc.)
- Elevate risk classification based on detected patterns
- Block interactive commands that need a TTY (`vim`, `less`, `top`, `git rebase -i`)
- Enforce timeout, kill child process group on expiry

### 6.4 Configurable Policy

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
`src/agent/tools/ShellExecuteTool.hpp/cpp`, `src/agent/AgentConfig.hpp`

---

## Phase 7: Context Management (**COMPLETE**)

**Goal:** Keep conversations within the LLM's context window during long coding sessions.

**Status:** Fully implemented. TokenEstimator provides character-based heuristic token counting
(~4 chars/token for natural language, ~3.5 for code). ConversationHistory tracks estimated tokens
across all messages. ConversationCompactor performs LLM-driven summarization when the conversation
approaches 80% of the context window, preserving system prompt and recent messages (including
tool result chains). Tool results are truncated at 30 KB (configurable via `max_tool_result_size`
in `agent.yml`). ProjectFileTree generates git-aware file trees. ProjectContextLoader loads
project rules (CLAUDE.md, AGENT.md, .endo/agent-rules.md), global rules
(~/.config/endo/agent-rules/*.md), and memory files (~/.config/endo/agent-memory/*.md).
SystemPromptBuilder assembles all context into a structured system prompt with sections for
global rules, project rules, environment, project structure, and agent memory.

### 7.1 Token Tracking

- `TokenEstimator` — pure utility functions for estimating token counts
- `estimateTokenCount(text)` — character-based heuristic with code detection
- `estimateTokenCount(ChatMessage)` — per-message overhead + content block estimates
- `ConversationHistory::estimatedTokenCount()` — running total, updated on add/clear/replace
- `ConversationHistory::replaceMessages()` — atomic replacement with token recalculation

### 7.2 Conversation Compaction

- `ConversationCompactor` — LLM-driven summarization when approaching context limit
- Trigger at 80% of `contextSize` (configurable via `CompactionConfig::triggerThreshold`)
- Preserves system prompt, last N messages, and pending tool result chains
- Summary injected as system message between original system prompt and preserved messages
- `AgentSession::setCompactionConfig()` creates compactor; runs before each provider call

### 7.3 Tool Result Truncation

- Truncate large tool results before adding to conversation (default 30 KB)
- `[truncated -- X bytes omitted]` marker appended
- Configurable via `AgentConfig::maxToolResultSize` and `agent.yml`: `max_tool_result_size: 30720`

### 7.4 Project Context

- `ProjectFileTree` — git-aware condensed file tree using `git ls-files`
- `ProjectContextLoader` — loads rules files, global rules, and memory files
- Rules file search: `CLAUDE.md`, `AGENT.md`, `.endo/agent-rules.md`
- Global rules: `~/.config/endo/agent-rules/*.md`
- Memory files: `~/.config/endo/agent-memory/*.md`

### 7.5 System Prompt Assembly

`SystemPromptBuilder` extended with new sections (all optional, omitted when empty):

1. Base instructions (built-in default or custom)
2. Global Rules section
3. Project Rules section
4. Environment section (CWD, git, shell)
5. Project Structure section (file tree)
6. Agent Memory section

**Touches:** `src/agent/TokenEstimator.hpp/cpp`, `src/agent/ConversationCompactor.hpp/cpp`,
`src/agent/ProjectFileTree.hpp/cpp`, `src/agent/ProjectContextLoader.hpp/cpp`,
`src/agent/ConversationHistory.hpp/cpp`, `src/agent/AgentSession.hpp/cpp`,
`src/agent/SystemPromptBuilder.hpp/cpp`, `src/agent/AgentConfig.hpp/cpp`,
`src/shell/Shell.cpp`

---

## Phase 7b: Interactive Authentication (**COMPLETE**)

**Goal:** Provide CLI commands for authenticating with LLM providers, persisting API keys,
and switching between providers — mirroring the UX of `claude` CLI and `opencode`.

**Status:** Fully implemented. `endo agent login/status/switch/logout` subcommands with
interactive provider selection, browser-based API key page opening, hidden input for key
entry, key validation via lightweight API call, atomic config file save, and stored-key
priority over env var fallback. 13 new test cases added (config save/load round-trips,
api_key parsing, provider key resolution). 187 total agent test cases (627 assertions),
all passing.

### 7b.1 Config Extensions

- Added `apiKey` field to `ClaudeConfig`, `OpenAiConfig`, `GeminiConfig` structs
- `resolveProviderApiKey()` checks stored key first, env var fallback
- `saveAgentConfig()` with atomic write (`.tmp` + `rename()` pattern)
- Only non-default fields emitted to keep YAML clean

### 7b.2 CLI Subcommands

| Command | Behavior |
|---------|----------|
| `endo agent login [PROVIDER]` | Interactive login: select provider, open browser to API key page, paste key (hidden input), validate, save |
| `endo agent status` | Show all providers with auth status, key source, and active marker |
| `endo agent switch [PROVIDER]` | Switch active provider (interactive menu or direct) |
| `endo agent logout [PROVIDER]` | Remove stored API key for a provider |

### 7b.3 Key Validation

Lightweight `GET` request to each provider's models endpoint before saving:
- Claude: `GET /v1/models` with `x-api-key` header
- OpenAI: `GET /v1/models` with `Authorization: Bearer` header
- Gemini: `GET /v1beta/models?key=...`

### 7b.4 Terminal Utilities

- `readSecretLine()` — disables terminal echo via `termios` (POSIX) / `SetConsoleMode` (Windows)
- `openBrowser()` — `xdg-open` (Linux) / `open` (macOS) / `start` (Windows)

**Files:** `src/agent/LoginCommand.hpp/cpp`, `src/agent/TerminalInput.hpp/cpp`,
`src/agent/AgentConfig.hpp/cpp` (extended), `src/agent/ProviderFactory.cpp` (updated),
`src/shell/main.cpp` (subcommand routing + help text)

---

## Phase 8: MCP Support

**Goal:** Enable the agent to use external MCP (Model Context Protocol) servers alongside
built-in tools.

### 8.1 MCP Client — Stdio Transport (**COMPLETE**)

Adapted from mychat's `src/mcp/` directory:

```
src/agent/mcp/
├── McpError.hpp              # Error types (McpErrorCode, McpError, McpResult<T>)
├── Transport.hpp             # Abstract transport interface
├── StdioTransport.hpp/.cpp   # Stdio pipe transport (posix_spawnp + pipes)
├── JsonRpc.hpp/.cpp          # JSON-RPC 2.0 helpers
├── McpClient.hpp/.cpp        # MCP protocol client (initialize, listTools, callTool)
├── ServerManager.hpp/.cpp    # Multi-server routing (tool name → server dispatch)
└── McpToolAdapter.hpp/.cpp   # Bridges MCP tools into AgentTool/ToolRegistry
```

- Adapted `Result<T>` → `std::expected<T, McpError>` (custom `McpError` type)
- Adapted namespace `mychat` → `endo::agent::mcp`
- `McpToolAdapter` wraps each MCP tool as an `AgentTool` for unified tool dispatch
- Unit tests: `JsonRpc_test.cpp`, `McpClient_test.cpp`, `ServerManager_test.cpp`, `McpToolAdapter_test.cpp`

### 8.2 Configuration (**COMPLETE** — shell builtins)

Configured via shell builtins in `~/.config/endo/init.endo`:

```endo
# Add MCP servers
add_mcp_server "filesystem" "npx -y @modelcontextprotocol/server-filesystem /home/user"
add_mcp_server "github" "npx -y @modelcontextprotocol/server-github"
set_mcp_env "github" "GITHUB_TOKEN" "$GITHUB_TOKEN"

# Remove a server
remove_mcp_server "filesystem"
```

Builtins: `add_mcp_server`, `set_mcp_env`, `remove_mcp_server`.
MCP servers are started when entering agent mode and shut down when leaving.

### 8.3 HTTP/SSE Transport

Add `HttpTransport` using the existing `HttpClient`:

```
src/agent/mcp/
└── HttpTransport.hpp/.cpp    # Streamable HTTP transport (MCP over SSE)
```

- Implement MCP Streamable HTTP transport (POST for requests, SSE for notifications)
- Support session management via `Mcp-Session-Id` header
- Leverage existing `HttpClient::executeStreaming()` (from Phase 1.5)

### 8.4 Dynamic Tool Discovery

- Handle `notifications/tools/list_changed` from MCP servers
- Re-fetch tool list on notification
- Update `ServerManager` routing map dynamically
- Notify `AgentSession` of tool set changes

**Touches:** new `src/agent/mcp/` directory, `src/agent/AgentSession.hpp/cpp`,
`src/agent/AgentConfig.hpp`, `CMakeLists.txt`

---

## Phase 9: Agent TUI Enhancements

**Goal:** Polish the inline agent experience with visualization and navigation.

### 9.1 Slash Commands

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

### 9.2 Tool Execution Visualization

- Show a status line per tool call: tool name, arguments summary, elapsed time
- Use existing `Spinner` with tool-specific labels during execution
- Display tool result summary (success/error, output size) after completion
- Diff rendering for `edit_file` results (green additions, red deletions)

### 9.3 Conversation History Navigation

- Scrollback through previous agent exchanges
- `Ctrl+Up`/`Ctrl+Down` to cycle through conversation turns
- Search through conversation history (`Ctrl+R` in agent mode)

### 9.4 Alt-Screen Fullscreen Agent View

Available on demand for focused agent interaction:

- `F11` or `/fullscreen` toggles alt-screen agent view
- Split layout: conversation pane + file preview pane
- Rich rendering using existing `Screen` (Fullscreen viewport mode)
- `Escape` or `q` returns to inline primary screen

**Touches:** `src/agent/AgentInputComponent.hpp/cpp`, `src/agent/AgentResponseComponent.hpp/cpp`,
new `src/agent/SlashCommandRegistry.hpp/cpp`, `src/tui/MarkdownRenderer.hpp/cpp`,
`src/agent/voice/VoiceInputManager.hpp/cpp` (voice status integration)

---

## Phase 10: Advanced Agent Features

**Goal:** Evolve from a single-turn tool caller into a sophisticated coding agent.

### 10.1 Parallel Tool Execution

Upgrade `AgentSession::executeToolCalls()`:

- Identify independent tool calls (no shared state dependencies)
- Execute independent calls concurrently using `std::jthread` or `std::async`
- Collect results and return in original order
- Serial fallback for tools with side effects on shared state

### 10.2 Agent-Powered Shell Completion

Use the LLM to enhance shell completions:

- When standard completion yields no results, offer to query the agent
- Agent suggests commands based on natural language intent
- Integrate with existing `CompletionPopup` — agent suggestions labeled `[AI]`

### 10.3 Error Recovery Suggestions

When a shell command fails (non-zero exit code):

- Offer inline agent analysis: "Want me to explain this error? `[y/n]`"
- Agent reads the error output (via Contour VT extension or captured stderr)
- Suggests corrective commands or code fixes
- User can accept suggestions directly into the prompt

### 10.4 Plan Mode

**Goal:** Add a structured planning workflow where the agent explores the codebase and
produces a step-by-step implementation plan for user approval before making any mutations.
This prevents wasted work, gives the user control over the approach, and surfaces
architectural decisions early.

#### 10.4.1 Activation

Plan mode is activated in three ways:

| Trigger | Behavior |
|---------|----------|
| `/plan <description>` | Explicit slash command — agent enters plan mode immediately |
| Auto-detection | Agent detects a complex request (multi-file, architectural decision, ambiguous) and proposes: "This looks like it needs planning. Enter plan mode? `[y/n]`" |
| `plan_mode: auto` config | Always plan before mutating tools — the agent explores first, then presents a plan |

#### 10.4.2 Plan Mode Agent Loop

In plan mode, the agent operates in a **read-only exploration phase** followed by a
**plan presentation phase**:

```
User submits request (or /plan command)
  │
  ├── Phase A: Exploration (read-only tools only)
  │    ├── Agent uses: read_file, glob, grep, git (read ops)
  │    ├── Agent CANNOT use: write_file, edit_file, shell_execute (mutations blocked)
  │    ├── Agent builds understanding of codebase, identifies relevant files
  │    ├── Streaming thinking shown inline: "Reading src/agent/AgentSession.cpp..."
  │    └── Exploration loop runs until agent has enough context
  │
  ├── Phase B: Plan Generation
  │    ├── Agent produces a structured plan (see Plan Data Model below)
  │    ├── Plan rendered inline with numbered steps, file paths, risk assessment
  │    └── Each step has: description, files touched, estimated complexity
  │
  └── Phase C: User Review
       ├── [y]es    — Approve and execute the plan
       ├── [n]o     — Reject, return to agent input for a different approach
       ├── [e]dit   — Open plan in InputField for inline editing, then re-approve
       └── [r]evise — Give feedback, agent revises the plan (loops back to B)
```

#### 10.4.3 Plan Data Model

```cpp
/// A single step in an agent's implementation plan.
struct PlanStep {
    size_t index;                          ///< 1-based step number.
    std::string description;               ///< What this step does (imperative form).
    std::vector<std::string> filesTouched; ///< File paths that will be created or modified.
    std::string rationale;                 ///< Why this step is needed (optional).
    std::vector<size_t> dependsOn;         ///< Indices of steps that must complete first.
};

/// The agent's proposed implementation plan.
struct Plan {
    std::string summary;                   ///< One-line summary of the overall approach.
    std::vector<PlanStep> steps;           ///< Ordered steps to execute.
    std::string riskAssessment;            ///< Potential risks and mitigations.
    std::vector<std::string> alternatives; ///< Alternative approaches considered (optional).
};

/// Status of a plan step during execution.
enum class PlanStepStatus : uint8_t {
    Pending,     ///< Not yet started.
    InProgress,  ///< Currently being executed.
    Completed,   ///< Successfully completed.
    Failed,      ///< Failed — agent will attempt recovery or ask user.
    Skipped,     ///< Skipped by user request or dependency failure.
};
```

The agent outputs the plan as a JSON tool call (`submit_plan`) which the session
parses into a `Plan` struct. This avoids fragile markdown parsing and lets the
renderer present the plan with consistent formatting.

#### 10.4.4 Plan Execution

After user approval, `PlanExecutor` drives the agent through the plan step by step:

```cpp
class PlanExecutor {
public:
    PlanExecutor(AgentSession& session, Plan plan);

    /// Execute the next pending step. Returns the updated step status.
    /// The agent receives: "Execute step N: <description>" as a user message,
    /// with full tool access (mutations enabled).
    std::expected<PlanStepStatus, AgentError> executeNextStep(StreamCallback streamCb);

    /// Current execution state.
    Plan const& plan() const;
    size_t currentStepIndex() const;
    PlanStepStatus stepStatus(size_t index) const;
    bool isComplete() const;

    /// User intervention points.
    void skipStep(size_t index);
    void pauseExecution();
    void resumeExecution();
};
```

**Execution behavior:**

- Each step is injected as a focused user message: `"Execute step 3/7: Add error handling to AgentSession::processMessage. Files: src/agent/AgentSession.cpp"`
- The agent gets full tool access during execution (write, edit, shell_execute)
- After each step, the executor checks the result and updates `PlanStepStatus`
- If a step fails, the agent can retry once or ask the user for guidance
- The user can pause between steps (`Ctrl+C` → pause menu), skip steps, or abort

#### 10.4.5 Plan Mode TUI Rendering

Plan mode renders inline in the primary screen:

**Exploration phase:**
```
╭─ agent │ claude/claude-sonnet-4-5-20250929 │ plan mode │ main
│ ⠋ Exploring codebase...
│   Reading src/agent/AgentSession.hpp
│   Searching for "processMessage" across src/agent/
│   Reading src/shell/Shell.cpp (lines 1063-1261)
```

**Plan presentation:**
```
╭─ plan │ 7 steps │ ~5 files
│
│  Summary: Add plan mode to the agent with read-only exploration,
│  structured plan output, and step-by-step execution.
│
│  1. ☐ Create Plan data model in src/agent/Plan.hpp
│     Files: src/agent/Plan.hpp (new)
│
│  2. ☐ Add submit_plan tool to ToolRegistry
│     Files: src/agent/tools/SubmitPlanTool.hpp/.cpp (new)
│
│  3. ☐ Implement PlanExecutor for step-by-step execution
│     Files: src/agent/PlanExecutor.hpp/.cpp (new)
│
│  4. ☐ Add plan mode loop to AgentSession
│     Files: src/agent/AgentSession.hpp/.cpp
│
│  5. ☐ Render plan and step progress in TUI
│     Files: src/agent/AgentResponseRenderer.hpp/.cpp
│
│  6. ☐ Wire /plan slash command into Shell::runAgentMode()
│     Files: src/shell/Shell.cpp
│
│  7. ☐ Add unit tests for plan mode
│     Files: src/agent/test-agent-session.cpp
│
│  Risk: Low — additive changes, no existing behavior modified.
│
│  [y]es  [n]o  [e]dit  [r]evise
```

**During execution:**
```
╭─ plan │ step 3/7 │ Implementing PlanExecutor
│ ✓ 1. Create Plan data model
│ ✓ 2. Add submit_plan tool
│ ⠋ 3. Implement PlanExecutor — writing src/agent/PlanExecutor.cpp...
│ ☐ 4. Add plan mode loop to AgentSession
│ ☐ 5. Render plan and step progress
│ ☐ 6. Wire /plan slash command
│ ☐ 7. Add unit tests
```

#### 10.4.6 AgentSession Integration

`AgentSession::processMessage()` gains a mode parameter:

```cpp
enum class SessionMode : uint8_t {
    Normal,    ///< Full tool access, direct execution (current behavior).
    PlanOnly,  ///< Read-only tools + submit_plan. No mutations allowed.
};

// Extended processMessage signature
auto processMessage(std::string_view userMessage, StreamCallback streamCb,
                    SessionMode mode = SessionMode::Normal)
    -> std::expected<std::string, AgentError>;
```

In `PlanOnly` mode:
- `ToolRegistry::definitions()` filters out mutating tools (write_file, edit_file, shell_execute)
- The `submit_plan` pseudo-tool is added to the tool definitions
- When the agent calls `submit_plan`, the session extracts the `Plan` and returns it
  via a special `AgentError` variant or a dedicated return type
- The caller (Shell::runAgentMode) catches the plan and enters the review/execution flow

#### 10.4.7 Configuration

In `~/.config/endo/agent.yml`:

```yaml
plan_mode:
  enabled: true
  auto_detect: true           # Agent proposes plan mode for complex requests
  require_approval: true      # Always require user approval before execution
  pause_between_steps: false  # Pause after each step for user review
  max_exploration_turns: 15   # Max tool-loop iterations during exploration phase
```

**Touches:** new `src/agent/Plan.hpp`, new `src/agent/PlanExecutor.hpp/cpp`,
`src/agent/AgentSession.hpp/cpp` (SessionMode, plan-only filtering),
`src/agent/tools/ToolRegistry.hpp/cpp` (filtering by risk level),
`src/agent/AgentResponseRenderer.hpp/cpp` (plan rendering),
`src/shell/Shell.cpp` (plan mode activation in `runAgentMode()`),
`src/agent/AgentConfig.hpp/cpp` (plan_mode config section)

### 10.5 Memory System

Persistent agent memory across sessions:

- Store key learnings, project conventions, user preferences in `~/.config/endo/agent-memory/`
- Auto-save when the agent discovers stable patterns
- Load memory into system prompt on session start
- Organize by project (project-level memory files)

**Touches:** `src/agent/AgentSession.hpp/cpp`, `src/agent/ConversationHistory.hpp/cpp`,
`src/shell/PromptComponent.hpp/cpp` (completion integration),
`src/agent/LlamaCppProvider.hpp/cpp` (local model parallel inference)

---

## Phase 11: Multi-Agent Teams

**Goal:** Enable the user to create a team of AI agents that collaborate on complex tasks.
Each agent has an assigned role, one agent serves as the team leader that supervises and
coordinates the others. Agents communicate via a structured message-passing system.

### 11.1 Team Data Model

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

### 11.2 Team Leader Agent

The team leader is a distinguished agent responsible for:

- **Task decomposition** — Breaking the user's request into sub-tasks
- **Task assignment** — Assigning sub-tasks to teammate agents based on their roles
- **Progress monitoring** — Tracking task completion, detecting blockers
- **Result aggregation** — Collecting teammate outputs and synthesizing a final response
- **Conflict resolution** — When teammates produce contradictory results

The leader has an augmented system prompt with team-management instructions and access to
team-coordination tools (see 9.4). The leader's `AgentSession` receives teammate messages
as tool results, keeping the coordination within the standard agent loop.

### 11.3 Cross-Agent Communication — Message Bus

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

### 11.4 Team-Coordination Tools

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

### 11.5 Shared Task Board

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

### 11.6 Agent Lifecycle

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

### 11.7 Team TUI Rendering

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

### 11.8 Configuration

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
`src/agent/tools/ToolRegistry.hpp/cpp` (team tools), `src/agent/AgentConfig.hpp/.cpp`,
`src/agent/SlashCommandRegistry.hpp/cpp` (`/team` commands), `CMakeLists.txt`

---

## Phase 12: Codebase Intelligence & Exploration

**Goal:** Give the agent structured codebase understanding beyond text-based grep — symbol-level
navigation, file outlines, exploration context isolation, and optional semantic search. This
bridges the gap between raw file tools (Phase 5) and the kind of deep code comprehension that
makes agentic coding assistants effective on large, unfamiliar projects.

**Status:** Not started.

### 12A — Tree-sitter Symbol Extraction

**Goal:** Provide symbol-level codebase navigation using tree-sitter for fast, accurate AST
parsing across languages. This is the foundation for all code intelligence features.

**Why tree-sitter over ctags:** Tree-sitter provides full AST access — not just symbol names
but also signatures, doc comments, nesting structure, and structural outlines. It parses
incrementally and tolerates syntax errors, making it suitable for in-progress code.

```
src/agent/intelligence/
├── TreeSitterParser.hpp/.cpp       # tree-sitter wrapper: load grammars, parse files
├── SymbolIndex.hpp/.cpp            # In-memory symbol table built from tree-sitter ASTs
├── SymbolExtractor.hpp/.cpp        # Language-specific AST queries for symbol extraction
└── grammars/                       # Bundled tree-sitter grammars (C/C++, Python, JS/TS, Rust, Go, etc.)
```

**Data model:**

```cpp
/// Classification of a code symbol.
enum class SymbolKind : uint8_t {
    Function,
    Method,
    Class,
    Struct,
    Enum,
    EnumVariant,
    Interface,
    Trait,
    Module,
    Namespace,
    Variable,
    Constant,
    TypeAlias,
    Macro,
    Field,
};

/// A symbol extracted from source code via tree-sitter.
struct SymbolInfo {
    std::string name;                    ///< Symbol name (e.g., "processMessage").
    SymbolKind kind;                     ///< Classification (function, class, etc.).
    std::string filePath;                ///< Absolute path to the source file.
    size_t line;                         ///< 1-based line number of the definition.
    size_t endLine;                      ///< 1-based end line (for range).
    std::string signature;               ///< Full signature (e.g., "auto processMessage(std::string_view, StreamCallback) -> std::expected<...>").
    std::string docComment;              ///< Extracted doc comment (Doxygen, JSDoc, etc.), if any.
    std::optional<std::string> parent;   ///< Enclosing symbol name (class for methods, namespace, etc.).
    std::vector<SymbolInfo> children;    ///< Nested symbols (class members, enum variants, etc.).
};
```

**New tools:**

| Tool | Input | Behavior |
|------|-------|----------|
| `find_symbol` | `{ name: string, kind?: string, language?: string }` | Find symbol definitions by name (exact or fuzzy). Returns file path, line, signature, doc comment. |
| `list_symbols` | `{ path: string, kind?: string }` | List all symbols in a file or directory, optionally filtered by kind. |
| `file_outline` | `{ path: string }` | Return a structural outline of a file: top-level symbols with their signatures, nesting, and line ranges. Compact — suitable for understanding file structure without reading the entire file. |

### 12B — LSP Client Integration (Optional)

**Goal:** Connect to running Language Server Protocol servers for precise, compiler-grade
navigation. This is optional — tree-sitter (12A) covers 80% of the value without requiring
users to have language servers installed.

**Dependency:** Phase 8 (MCP) shares JSON-RPC infrastructure (`src/agent/mcp/JsonRpc.hpp`).

```
src/agent/intelligence/
├── LspClient.hpp/.cpp              # LSP client: initialize, textDocument/* requests
└── LspManager.hpp/.cpp             # Discover, start, and manage LSP server processes
```

**Implementation notes:**

- Reuse `JsonRpc.hpp` from Phase 8 for the JSON-RPC 2.0 transport layer
- Communicate with LSP servers via stdio (same as MCP stdio transport)
- Auto-discover LSP servers from common locations and configuration
- Graceful fallback: if no LSP server is available, tools return an error suggesting tree-sitter alternatives

**New tools:**

| Tool | Input | Behavior |
|------|-------|----------|
| `goto_definition` | `{ path: string, line: number, column: number }` | Jump to the definition of the symbol at the given position. Returns target file path and line. |
| `find_references` | `{ path: string, line: number, column: number }` | Find all references to the symbol at the given position across the workspace. |
| `workspace_symbols` | `{ query: string }` | Search for symbols across the entire workspace by name pattern. |

### 12C — Exploration Context Isolation

**Goal:** Provide an `explore` tool that runs an isolated inner agent loop for codebase
exploration, returning only a concise summary to the main conversation. This prevents
20+ intermediate grep/read results from polluting the main context window.

**Priority:** Implement first — highest impact, lowest effort. Requires zero new dependencies;
reuses existing `AgentSession` infrastructure (Phase 5) and `ConversationHistory` (Phase 7).

```
src/agent/tools/
└── ExploreTool.hpp/.cpp             # Isolated exploration sub-agent
```

**How it works:**

1. The main agent calls `explore` with a natural-language question about the codebase
2. `ExploreTool` creates a temporary `AgentSession` with its own `ConversationHistory`
3. The inner session has access to read-only tools only (`read_file`, `glob`, `grep`, `git` read ops, `file_outline`, `find_symbol`, `list_symbols`)
4. The inner agent iterates (up to `max_exploration_turns`) until it has an answer
5. The inner session is discarded; only the final summary is returned as the tool result
6. The main conversation sees one tool call + one concise result — not the intermediate exploration

**Tool definition:**

| Tool | Input | Behavior |
|------|-------|----------|
| `explore` | `{ question: string, scope?: string, max_turns?: number }` | Run an isolated exploration sub-agent. `scope` narrows the search (e.g., `"src/agent/"`, `"*.cpp"`). Returns a concise summary answering the question. |

**Design decisions:**

- **Read-only enforcement:** The inner session's `ToolRegistry` excludes all mutating tools.
  This is a hard constraint, not a prompt instruction — the tools simply aren't available.
- **Context isolation:** The inner `ConversationHistory` is completely separate from the main
  conversation. The inner agent's system prompt includes the project file tree and rules
  (same as the main agent) but no prior conversation context.
- **Token budget:** The inner session has its own context window budget. If the inner agent
  approaches its limit, it summarizes what it's found so far and returns early.
- **Concurrency:** Only one `explore` call runs at a time (sequential). Future work (Phase 10.1
  parallel tools) could allow concurrent explorations.

### 12D — Codebase Indexing & Caching

**Goal:** Build and persist a symbol index so the agent doesn't start from scratch every session.
Background indexing runs non-blocking, following the `std::async` pattern from `runAgentMode()`.

**Dependency:** 12A (tree-sitter symbol extraction provides the data to index).

```
src/agent/intelligence/
├── IndexBuilder.hpp/.cpp           # Background indexer: walk files, extract symbols, build index
├── IndexCache.hpp/.cpp             # Persistent index storage (SQLite or flat file)
└── FileWatcher.hpp/.cpp            # inotify/kqueue watcher for incremental re-indexing
```

**Index lifecycle:**

1. **Initial build:** On first agent session in a project, index all source files in a background
   thread. The agent can work immediately with grep/read — index results become available
   progressively.
2. **Incremental update:** `FileWatcher` detects file changes (save, create, delete) and re-indexes
   only affected files. Uses `inotify` (Linux) / `kqueue` (macOS) / `ReadDirectoryChangesW` (Windows).
3. **Cache persistence:** Index is stored in `~/.cache/endo/index/<project-hash>/` as a compact
   binary or SQLite database. Invalidated by file modification timestamps.
4. **Warm start:** On subsequent sessions, load the cached index and verify freshness against
   filesystem mtimes. Stale entries are re-indexed in the background.

**Integration with tools:**

- `find_symbol` checks the index first (O(1) lookup), falls back to on-demand tree-sitter parse
- `workspace_symbols` (12B) can use the index for fast fuzzy matching when no LSP is available
- `file_outline` checks the index for cached outlines, re-parses on cache miss

### 12E — Semantic / Embedding Search (Optional)

**Goal:** Enable conceptual queries like "find the retry logic" or "where is authentication
handled" without knowing exact function names. Uses local embedding models to build a
semantic index of the codebase.

**Dependencies:** 12D (codebase indexing provides the chunking infrastructure) + Phase 3
(llama.cpp provides the model runtime for running embedding models locally).

```
src/agent/intelligence/
├── EmbeddingEngine.hpp/.cpp        # Local embedding model wrapper (via llama.cpp or ONNX)
├── SemanticIndex.hpp/.cpp          # Vector index for code chunks (HNSW or flat)
└── CodeChunker.hpp/.cpp            # Split source files into semantically meaningful chunks
```

**Implementation notes:**

- **Embedding model:** Use a small, code-optimized embedding model (e.g., `nomic-embed-code`,
  `jina-embeddings-v3`, or similar) via llama.cpp's embedding API or ONNX runtime
- **Chunking strategy:** Split files at function/class boundaries (using tree-sitter from 12A),
  not fixed token windows. Each chunk includes the symbol's signature, doc comment, and body.
- **Vector storage:** Lightweight HNSW index (e.g., hnswlib or usearch, header-only) stored
  alongside the symbol index in `~/.cache/endo/index/<project-hash>/`
- **Query flow:** User query → embed → k-NN search → return top-N code chunks with file paths
  and line ranges

**New tool:**

| Tool | Input | Behavior |
|------|-------|----------|
| `semantic_search` | `{ query: string, top_k?: number, scope?: string }` | Search the codebase by concept. Returns ranked code chunks with file paths, line ranges, and relevance scores. |

### 12.6 Configuration

In `~/.config/endo/agent.yml`:

```yaml
intelligence:
  # Tree-sitter (12A)
  tree_sitter:
    enabled: true
    languages: auto                  # auto-detect from file extensions, or explicit list
    max_file_size_kb: 512            # Skip files larger than this

  # LSP (12B)
  lsp:
    enabled: false                   # Opt-in — requires language servers to be installed
    servers:
      cpp: clangd
      python: pylsp
      typescript: typescript-language-server

  # Exploration (12C)
  explore:
    max_turns: 15                    # Max tool iterations for inner exploration agent
    provider: null                   # null = use main provider; or override with a cheaper model

  # Indexing (12D)
  indexing:
    enabled: true
    background: true                 # Index in background thread (non-blocking)
    cache_dir: ~/.cache/endo/index/
    watch_files: true                # Use file watcher for incremental updates
    exclude_patterns:                # Patterns to exclude from indexing
      - "build/"
      - "node_modules/"
      - ".git/"
      - "vendor/"

  # Semantic search (12E)
  semantic:
    enabled: false                   # Opt-in — requires embedding model
    model_path: ~/.local/share/endo/models/nomic-embed-code-q8_0.gguf
    chunk_size: 512                  # Target chunk size in tokens
    top_k: 10                        # Default number of results
```

### 12.7 Integration Points

| Existing Code | Integration |
|---------------|-------------|
| `ToolRegistry` (Phase 5) | Register `find_symbol`, `list_symbols`, `file_outline`, `explore`, optionally `goto_definition`, `find_references`, `workspace_symbols`, `semantic_search` |
| `Shell::runAgentMode()` | Initialize `TreeSitterParser` and `IndexBuilder` at session start; start background indexing |
| `AgentConfig` | Add `IntelligenceConfig` section for tree-sitter, LSP, indexing, and semantic settings |
| `SystemPromptBuilder` | Add tool descriptions for intelligence tools to the system prompt |
| `AgentSession` (Phase 7) | `ExploreTool` creates a temporary `AgentSession` for isolated exploration |
| `ConversationHistory` (Phase 7) | Inner exploration sessions use their own `ConversationHistory` instance |
| `JsonRpc` (Phase 8) | LSP client reuses JSON-RPC 2.0 transport for language server communication |
| `LlamaCppProvider` (Phase 3) | Semantic search uses llama.cpp embedding API for local embeddings |

**Touches:** new `src/agent/intelligence/` directory, new `src/agent/tools/ExploreTool.hpp/cpp`,
`src/agent/tools/ToolRegistry.hpp/cpp` (register intelligence tools),
`src/agent/AgentConfig.hpp/.cpp` (intelligence config section),
`src/agent/SystemPromptBuilder.hpp/cpp` (tool descriptions),
`src/shell/Shell.cpp` (intelligence initialization in `runAgentMode()`),
`CMakeLists.txt` (tree-sitter dependency, optional hnswlib/usearch)

---

## Dependency Graph

```
Phase 1 (LLM Providers + Multimodal)
   │
   ├──→ Phase 2 (Agent Mode UX + Image I/O)
   │         │
   │         ├──→ Phase 5 (Tool System) ──→ Phase 6 (Permissions)
   │         │         │
   │         │         ├──→ Phase 10 (Advanced Features)
   │         │         │       ├── 10.1 Parallel Tools
   │         │         │       ├── 10.2 AI Completion
   │         │         │       ├── 10.3 Error Recovery
   │         │         │       ├── 10.4 Plan Mode
   │         │         │       └── 10.5 Memory System
   │         │         │
   │         │         ├──→ Phase 11 (Multi-Agent Teams)
   │         │         │
   │         │         └──→ Phase 12 (Codebase Intelligence & Exploration)
   │         │                  ├── 12C Explore Tool (Phase 5 + 7 only)
   │         │                  ├── 12A Tree-sitter (Phase 5 only)
   │         │                  ├── 12B LSP Client (Phase 8 — JSON-RPC)
   │         │                  ├── 12D Indexing (12A)
   │         │                  └── 12E Semantic Search (12D + Phase 3)
   │         │
   │         └──→ Phase 9 (TUI Enhancements)
   │
   ├──→ Phase 3 (Local Models — llama.cpp)
   │
   ├──→ Phase 4 (Voice Input — whisper.cpp + VAD) ──→ Phase 9 (TUI Enhancements)
   │
   ├──→ Phase 7 (Context Management)
   │
   └──→ Phase 8 (MCP Support)
```

- **Phase 1** is the prerequisite — LLM provider support (including multimodal content model) unblocks all agentic features.
- **Phase 2** (UX) should come next — establishes the interaction model, including image paste input (2.6) and image output rendering (2.7).
- **Phase 3** (Local Models) is independent after Phase 1 — adds a new `LlmProvider` backend using llama.cpp for offline inference. Shares GPU acceleration infrastructure with Phase 4.
- **Phase 4** (Voice Input) is independent after Phase 1 — uses whisper.cpp for speech-to-text with VAD. Shares GPU backends with Phase 3 and feeds into TUI enhancements (Phase 9).
- **Phase 5** (Tools) and **Phase 7** (Context) can proceed in parallel after Phase 2.
- **Phase 6** (Permissions) depends on Phase 5 (tools must exist to classify).
- **Phase 8** (MCP) is independent after Phase 1 — mychat code provides a head start.
- **Phase 9** (TUI) depends on Phase 2 (agent UX must exist to enhance) and benefits from Phase 4 (voice status rendering).
- **Phase 10** (Advanced) depends on Phases 5 and 6 being in place.
- **Phase 11** (Multi-Agent Teams) depends on Phase 5 (tools), Phase 6 (permissions — each agent needs risk caps), and Phase 7 (context management — each agent needs its own conversation). This is the capstone feature.
- **Phase 12** (Codebase Intelligence) depends on Phase 5 (tool system). Sub-phase 12C (explore tool) also needs Phase 7 (context management for isolated sessions). Sub-phase 12B (LSP) reuses Phase 8's JSON-RPC. Sub-phase 12E (semantic search) requires Phase 3 (llama.cpp for local embeddings) and 12D (indexing). **Recommended implementation order:** 12C → 12A → 12D → 12B → 12E.

---

## Target Directory Structure

```
src/agent/
├── Types.hpp                       # Core types: ChatMessage, ContentBlock, ToolCall, etc.
├── LlmProvider.hpp                 # Abstract provider interface (multimodal)
├── ClaudeProvider.hpp/.cpp         # Anthropic Claude API (SSE streaming)
├── OpenAiProvider.hpp/.cpp         # OpenAI-compatible API (OpenAI, Ollama, vLLM, LM Studio)
├── GeminiProvider.hpp/.cpp         # Google Gemini API (SSE streaming, image output)
├── LlamaCppProvider.hpp/.cpp       # Local inference via llama.cpp (Phase 3)
├── ModelManager.hpp/.cpp           # GGUF model discovery, loading, memory management (Phase 3)
├── ProviderFactory.hpp/.cpp        # Multi-provider creation and runtime switching
├── AgentConfig.hpp/.cpp            # Configuration data model + YAML loading
├── AgentSession.hpp/.cpp           # Multi-step agent loop (from mychat AgentLoop)
├── ConversationHistory.hpp/.cpp    # Conversation management (from mychat ChatSession)
├── PermissionManager.hpp/.cpp      # Tool risk classification and approval
├── AgentInputComponent.hpp/.cpp    # Styled agent input (purple bar, image paste, voice)
├── AgentResponseComponent.hpp/.cpp # Streaming response renderer (text + inline images)
├── SlashCommandRegistry.hpp/.cpp   # /commit, /review, /test, /team, /model, /voice, etc.
├── tools/
│   ├── AgentTool.hpp               # Abstract tool interface
│   ├── ToolRegistry.hpp/.cpp       # Built-in tool registry
│   ├── ReadFileTool.hpp/.cpp
│   ├── WriteFileTool.hpp/.cpp
│   ├── EditFileTool.hpp/.cpp
│   ├── GlobTool.hpp/.cpp
│   ├── GrepTool.hpp/.cpp
│   ├── ShellExecuteTool.hpp/.cpp
│   ├── GitTool.hpp/.cpp
│   └── ExploreTool.hpp/.cpp        # Isolated exploration sub-agent (12C)
├── voice/                          # Voice input subsystem (Phase 4)
│   ├── WhisperEngine.hpp/.cpp      # whisper.cpp wrapper for speech-to-text
│   ├── AudioCapture.hpp/.cpp       # Microphone input (PulseAudio/ALSA/CoreAudio/WASAPI)
│   ├── VoiceActivityDetector.hpp/.cpp  # VAD (Silero ONNX / energy-based fallback)
│   └── VoiceInputManager.hpp/.cpp  # Coordinates capture → VAD → transcription → input
├── intelligence/
│   ├── TreeSitterParser.hpp/.cpp   # tree-sitter wrapper: load grammars, parse files (12A)
│   ├── SymbolIndex.hpp/.cpp        # In-memory symbol table from tree-sitter ASTs (12A)
│   ├── SymbolExtractor.hpp/.cpp    # Language-specific AST queries for symbol extraction (12A)
│   ├── LspClient.hpp/.cpp          # LSP client: initialize, textDocument/* requests (12B)
│   ├── LspManager.hpp/.cpp         # Discover, start, manage LSP server processes (12B)
│   ├── IndexBuilder.hpp/.cpp       # Background indexer: walk files, extract symbols (12D)
│   ├── IndexCache.hpp/.cpp         # Persistent index storage (SQLite or flat file) (12D)
│   ├── FileWatcher.hpp/.cpp        # inotify/kqueue watcher for incremental re-indexing (12D)
│   ├── EmbeddingEngine.hpp/.cpp    # Local embedding model wrapper (12E)
│   ├── SemanticIndex.hpp/.cpp      # Vector index for code chunks (HNSW) (12E)
│   ├── CodeChunker.hpp/.cpp        # Split source files into semantic chunks (12E)
│   └── grammars/                   # Bundled tree-sitter grammars (C/C++, Python, JS/TS, etc.)
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

## Documentation (**COMPLETE**)

Agent documentation added to the mkdocs site under `docs/agent/`:

- **Overview** (`agent/index.md`) — What the agent is, providers, quick start, CLI commands
- **Configuration** (`agent/configuration.md`) — `agent.yml` reference, MCP server setup, web search config
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

1. Press `#` in the endo shell to activate agent mode
2. Ask the agent to read, understand, and modify code — all inline
3. Paste an image from the clipboard (`Ctrl+V`) and ask the agent about it — with inline sixel preview
4. See LLM-generated images rendered inline in the terminal (Gemini, OpenAI)
5. Run a local LLM via llama.cpp (`provider: local`) — fully offline, no API key required
6. Dictate a query via voice (`Ctrl+Space` push-to-talk) and see real-time transcription
7. See the agent plan changes, execute tools (including shell commands), and iterate
8. Review diffs, approve mutations, and commit results — without leaving the shell
9. Use MCP servers to extend the agent's capabilities
10. Work through long sessions without context window issues
11. Create a team of agents (`/team create`) with assigned roles and a leader, watch them collaborate on complex tasks, and receive a synthesized result
12. Ask the agent to explore an unfamiliar codebase — it navigates symbols, outlines files, and answers questions without flooding the conversation with raw grep output
13. Return to normal shell mode with `Escape` at any time
