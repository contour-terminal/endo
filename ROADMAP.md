# Endo Shell - Roadmap

## Executive Summary

Endo Shell is a modern interactive shell designed as a companion to Contour Terminal. It prioritizes
first-class user experience, IDE-like features, and modern terminal capabilities while maintaining
reasonable compatibility with Bash syntax.

This roadmap outlines the development path from current state to a feature-complete 1.0 release.
**Note:** This document describes priorities and dependencies, not timelines. Contour Terminal remains
the primary project; Endo development follows as resources permit.

## Vision

- **Modern UX**: Rich text editing, mouse support, LSP-like completions, and syntax highlighting
- **AI-Powered**: Natural language command generation with provider abstraction (local LLMs and cloud APIs)
- **Cross-Platform**: First-class Windows support alongside Linux/macOS
- **Developer-Friendly**: Debug Adapter Protocol (DAP) integration for script debugging

---

## Current Status

### Fully Implemented

| Component | Status |
|-----------|--------|
| Lexer with shell syntax tokens | ✅ |
| Parser (if/while, pipes, commands, redirects) | ✅ |
| AST with visitor pattern | ✅ |
| IR generation to CoreVM bytecode | ✅ |
| Process execution (fork/exec) | ✅ |
| Multi-process pipes | ✅ |
| Builtins: `exit`, `true`, `false`, `read`, `cd`, `set`, `unset`, `export`, `bind`, `echo` | ✅ |
| Environment variables (set/get/export) | ✅ |
| Variable substitution (`$VAR`, `${VAR}`, `$?`, `$$`, `$!`, `$0-$9`) | ✅ |
| Command substitution (`$(cmd)`, `` `cmd` ``) | ✅ |
| Process substitution (`<(cmd)`, `>(cmd)`) | ✅ |
| Logical operators (`&&`, `||`) | ✅ |
| Redirects (`>`, `>>`, `<`, `2>&1`, `<<<`) | ✅ |
| If-then-else-elif-fi statements | ✅ |
| While-do-done statements | ✅ |
| For-in loops (`for var in list; do ...; done`) | ✅ |
| Case statements (`case ... esac`) | ✅ |
| Function definitions (`function name() {}`, `name() {}`) | ✅ |
| Break and continue statements | ✅ |
| Return statement | ✅ |
| TTY abstraction with raw mode | ✅ |
| Platform abstraction layer (Pipe, Process, TTY) | ✅ |
| Grapheme cluster support for Unicode | ✅ |
| Tilde expansion (`~`, `~user`) | ✅ |
| Brace expansion (`{a,b,c}`, `{1..10}`) | ✅ |
| Parameter expansion (`${var:-default}`, `${#var}`, etc.) | ✅ |
| Arithmetic expansion (`$((expr))`) | ✅ |
| Pathname expansion (globbing) `*`, `?`, `[...]`, `**` | ✅ |
| Job management (`&`, `jobs`, `fg`, `bg`, `wait`) | ✅ |
| Builtin command: `cat` | TODO |

### Not Yet Implemented

See milestone breakdown below.

---

## Milestone 0: Foundation Solidification

**Priority:** Critical
**Rationale:** These foundational improvements enable all subsequent milestones and are required for
Windows support.

### 0.1 UTF-8 Support Completion in Lexer ✅

**Status:** Complete

**Tasks:**
- [x] Implement proper UTF-8 codepoint consumption in `consumeNumber()`
- [x] Implement proper UTF-8 handling in `consumeIdentifier()`
- [x] Implement proper UTF-8 handling in string literal parsing
- [x] Add tests for Unicode identifiers and string content

### 0.2 Platform Abstraction Layer ✅

**Status:** Complete (Windows stubs in place; full implementation deferred to Milestone 4)

**Tasks:**
- [x] Design platform abstraction interface for process management
- [x] Design platform abstraction interface for file descriptors and pipes
- [x] Design platform abstraction interface for TTY/console operations
- [x] Implement Linux/POSIX backend
- [x] Add CMake configuration for platform-specific compilation
- [x] Create Windows stubs (WindowsPipe, WindowsTTY, WindowsProcess)
- [ ] Implement Windows backend (ConPTY, CreateProcess) → Deferred to Milestone 4

### 0.3 Error Handling Modernization ✅

**Status:** Complete

**Tasks:**
- [x] Audit existing error handling
- [x] Introduce `std::expected` for recoverable errors
- [x] Create error type hierarchy for shell errors
- [x] Add structured error reporting with context (line/column, suggestions)

---

## Milestone 1: Core Language Features

**Priority:** High
**Rationale:** Completes the shell language to be practically useful for daily work.

### Phase 1.1: Variable System ✅

**Status:** Complete (local variable scope deferred until function support in Phase 1.6)

**Dependency:** None

**Tasks:**
- [x] Implement `$VAR` substitution in commands
- [x] Implement `${VAR}` extended substitution syntax
- [ ] Implement local variable scope (within functions) → Deferred to Phase 1.6
- [x] Implement global variable scope
- [x] Implement `unset` builtin
- [x] Add tests for variable scoping rules
- [x] Implement special variables: `$?`, `$$`, `$!`, `$0-$9`

### Phase 1.2: Redirects and File Descriptors ✅

**Status:** Complete

**Dependency:** Phase 1.1 (variables may appear in redirect targets)

**Tasks:**
- [x] Implement output redirect `>` and `>>`
- [x] Implement input redirect `<`
- [x] Implement file descriptor duplication `2>&1`
- [x] Implement here-documents `<<EOF` (parsing complete, content reading deferred)
- [x] Implement here-strings `<<<`
- [x] Integrate redirects with builtin commands (not just external processes)
- [x] Add comprehensive redirect tests

### Phase 1.3: Logical Operators ✅

**Status:** Complete

**Dependency:** None

**Tasks:**
- [x] Implement `&&` (AND) operator
- [x] Implement `||` (OR) operator
- [x] Implement proper short-circuit evaluation
- [x] Add operator precedence tests

### Phase 1.4: Command and Process Substitution ✅

**Status:** Complete

**Dependency:** Phase 1.2 (redirects), Phase 1.1 (variables)

**Tasks:**
- [x] Implement command substitution `$(command)`
- [x] Implement backtick substitution `` `command` ``
- [x] Implement process substitution `<(command)` (read)
- [x] Implement process substitution `>(command)` (write)
- [x] Handle nested substitutions
- [x] Add substitution tests

### Phase 1.5: Expansions ✅

**Status:** Complete

**Dependency:** Phase 1.1 (variables), Phase 1.4 (substitution for arithmetic)

**Tasks:**
- [x] Implement tilde expansion `~`, `~user`
- [x] Implement brace expansion `{a,b,c}`, `{1..10}`
- [x] Implement parameter expansion `${var:-default}`, `${var:+alt}`, `${#var}`, etc.
- [x] Implement arithmetic expansion `$((expr))`
- [x] Implement pathname expansion (globbing) `*`, `?`, `[...]`
- [x] Implement extended globbing `**` (recursive)
- [x] Define and document expansion order
- [x] Add expansion tests

**Implementation Notes:**
- Tilde expansion: `~` expands to `$HOME`, `~user` expands to user's home directory
- Brace expansion: Handled at parse time for efficiency (no runtime overhead)
- Parameter expansion: Supports length (`${#VAR}`), defaults (`${VAR:-default}`, `${VAR:=default}`, `${VAR:+alt}`, `${VAR:?error}`), prefix/suffix removal (`${VAR#pattern}`, `${VAR##pattern}`, `${VAR%pattern}`, `${VAR%%pattern}`), and replacement (`${VAR/pattern/replacement}`, `${VAR//pattern/replacement}`)
- Arithmetic expansion: Supports `+`, `-`, `*`, `/`, `%`, `**`, comparisons (`<`, `>`, `<=`, `>=`, `==`, `!=`), logical operators (`&&`, `||`, `!`), and bitwise operators
- Pathname expansion: Cross-platform implementation using `<filesystem>`, supports `*`, `?`, `[...]` bracket expressions with ranges and `**` recursive globbing

### Phase 1.6: Control Flow Completion ✅

**Status:** Complete (except `select` - deferred; C-style for loop requires arithmetic assignment)

**Dependency:** Phase 1.1 (variables for loop iteration)

**Tasks:**
- [x] Implement `for var in list; do ...; done`
- [ ] Implement `for ((init; cond; step)); do ...; done` → Deferred: requires arithmetic assignment expressions
- [x] Implement `case ... esac` pattern matching
- [ ] Implement `select` for menu generation → Deferred: requires TTY interaction
- [x] Implement function definitions `function name() { ... }` and `name() { ... }`
- [x] Implement `return` statement for functions
- [x] Implement `break` and `continue` for loops
- [x] Add control flow tests

**Implementation Notes:**
- For-list loops: Full support with break/continue, proper cleanup of nested loop state
- Case statements: Full glob-style pattern matching with multiple patterns per clause (`|`-separated)
- Functions: Supports positional parameters ($1, $2, ...) and return values affecting $?
- Functions are scoped to the current command execution (not persisted across separate execute() calls)
- C-style for loops and `select` require language features not yet implemented

### Phase 1.7: Job Management ✅

**Status:** Complete

**Dependency:** Platform abstraction (Milestone 0.2)

**Tasks:**
- [x] Implement background execution `&`
- [x] Implement `jobs` builtin
- [x] Implement `fg` builtin
- [x] Implement `bg` builtin
- [x] Implement `Ctrl+Z` suspend handling (external SIGTSTP/SIGCONT)
- [x] Implement job status notifications
- [x] Handle process groups correctly
- [x] Remember exit codes from all pipeline processes
- [x] Add job management tests

**Implementation Notes:**
- Background execution (`&`) spawns commands in a new process group and returns immediately
- `jobs` lists all background jobs with their state (Running, Stopped, Done)
- `fg` brings a background job to the foreground and waits for completion
- `bg` resumes a stopped job in the background
- `wait` waits for all or specific background jobs to complete
- `$!` contains the PID of the last background job
- Uses `signalfd` on Linux for race-free SIGCHLD, SIGTSTP, and SIGCONT handling
- Falls back to traditional signal handlers on macOS/BSD
- Process groups are properly managed for job control
- Foreground job control: When running a foreground command (single or pipeline), the shell creates a new process group, transfers terminal control to it, and waits with `WUNTRACED`. When Ctrl+Z is pressed, the process receives SIGTSTP, the shell detects the stopped state, adds the job to the job table, and returns control to the shell. The user can then use `fg` to resume or `bg` to continue in background.
- SIGTSTP handling for shell itself: When the shell receives SIGTSTP (e.g., from parent shell via `kill -TSTP`), it restores terminal to cooked mode, re-raises SIGTSTP with default handling to actually stop, and when resumed (SIGCONT), restores raw mode and redraws the prompt
- Note: Ctrl+Z at the prompt (when no foreground job is running) is used for undo (TUI feature)
- Job control builtins (`jobs`, `fg`, `bg`, `wait`) are recognized as parser directives with dedicated AST nodes

---

## Milestone 2: Terminal UX

**Priority:** High
**Rationale:** Differentiates Endo from other shells; delivers on the "IDE-like" promise.

### Phase 2.1: Rich Text Editor Foundation

**Status:** Complete

**Dependency:** Milestone 1 complete (need full language for practical editing)

**Implementation Summary:** A comprehensive TUI library has been integrated into the project (`src/tui/`).
The library includes:
- Terminal input/output abstraction (`Terminal`, `TerminalInput`, `TerminalOutput`)
- VT sequence parser (`VtParser`) with support for CSI, SGR mouse, bracketed paste, UTF-8
- Input field with multiline editing, history, and kill ring (`InputField`)
- Various UI components (Box, Dialog, List, LogPanel, StatusBar, Spinner, Text, Theme)
- Sixel image support and Markdown rendering
- Configurable keybinding system (`KeyBindings`, `EditAction`) with modern defaults

**Tasks:**
- [x] Add GUI-style selection model (Shift+arrows, Ctrl+A select all, Ctrl+C copy, Ctrl+X cut)
- [x] Implement undo/redo history (Ctrl+Z undo, Ctrl+Y/Ctrl+Shift+Z redo)
- [x] Implement clipboard integration via OSC 52 (`TerminalOutput::copyToClipboard()` + callback)
- [x] Add mouse click-to-position cursor support (`InputField::setCursorFromClick()`)
- [x] Rewrite `Prompt` class to use `tui::Terminal` + `tui::InputField`
- [x] Remove `InputEditor` dependency from Shell (superseded by TUI library)
- [x] Implement fixed editor region that auto-grows up to 50% of terminal height
- [x] Add multiline editing support with proper rendering and selection highlighting
- [x] Add comprehensive editor unit tests (47 tests covering basic editing, cursor movement, selection, undo/redo, multiline, history, kill ring, clipboard, and UTF-8)
- [x] Implement configurable keybinding framework (`EditAction`, `KeyChord`, `KeyBindings`)

**Implementation Notes:**
- Multiline editing uses Alt+Enter or Shift+Enter to insert newlines (Enter submits)
- Editor region scrolls to keep cursor visible when content exceeds max height
- Selection highlighting uses inverse video (SGR 7/27)
- Display width calculation uses libunicode for proper Unicode handling
- Keybinding system maps key chords to edit actions, enabling future vi mode support
- Default keybindings use modern conventions: Ctrl+C=copy, Ctrl+Y=redo, Ctrl+D=delete char (EOF on empty)
- Shift+movement keys extend selection; Ctrl+D is context-sensitive (EOF vs delete)
- Kitty keyboard protocol support: Full handling of Kitty's CSIu escape sequences including:
  - CapsLock and NumLock modifiers (bits 6-7) for proper capitalization with CapsLock active
  - All special keycodes in Private Use Area (57344-63743): lock keys, F13-F35, keypad, media keys, modifier keys
  - CapsLock XOR Shift behavior: either one (but not both) capitalizes letters, matching standard keyboard behavior
- `bind` builtin command allows runtime keybinding management:
  - `bind` - List all keybindings
  - `bind <key> <action>` - Bind a key to an action (e.g., `bind ctrl+y yank`)
  - `bind -r <key>` - Remove a keybinding
  - `bind --reset` - Reset to defaults
  - `bind --help` - Show available actions and key format

### Phase 2.2: Mouse Integration

**Status:** Complete

**Dependency:** Phase 2.1

**Tasks:**
- [x] Implement passive mouse tracking VT extension support (DEC mode 2029)
- [x] Implement click-to-position cursor
- [x] Implement click-and-drag selection
- [x] Implement double-click word selection (fish-style word boundaries)
- [x] Implement triple-click line selection
- [x] Add mouse interaction tests

**Implementation Notes:**
- Uses Contour's passive mouse tracking (DEC mode 2029) which includes SGR format and uiHandled hint
- Word selection uses fish-style boundaries: path separators (`/`) and punctuation break words
- Events with `uiHandled=true` are skipped (terminal UI consumed them, e.g., for scrollback)
- Scroll wheel scrolls multiline editor content
- 14 new mouse-related tests added to InputField_test.cpp

### Phase 2.3: Completion and Suggestions

**Status:** Complete

**Dependency:** Phase 2.1, Milestone 1 (need language features to complete)

**Notes:**
- Completion system uses an abstraction layer (`CompletionProvider` interface) to allow both local and AI-powered completion providers
- Initial implementation provides local completion based on the current command line context
- Fish-style ghost text suggestions appear dimmed after the cursor
- Tab or Ctrl+Space triggers completion menu; Right arrow or End accepts ghost text

**Tasks:**
- [x] Design completion provider interface (`CompletionProvider`, `CompletionItem`, `CompletionContext`)
- [x] Implement context analysis (`CompletionContext.cpp` - uses Lexer to determine context type)
- [x] Implement command name completion (`CommandCompleter.cpp` - builtins + PATH scanning with caching)
- [x] Implement file path completion (`FileCompleter.cpp` - with tilde expansion)
- [x] Implement variable name completion (`VariableCompleter.cpp` - env vars + special vars)
- [x] Implement option/flag completion stub (`OptionCompleter.cpp` - placeholder for future --help parsing)
- [x] Implement history-based suggestions (`HistoryCompleter.cpp` - prefix matching with recency scoring)
- [x] Implement history abstraction (`History` interface, `InMemoryHistory` implementation)
- [x] Implement completer orchestrator (`Completer.cpp` - coordinates providers, generates suggestions)
- [x] Add ghost text support to InputField (`setGhostText()`, `acceptGhostText()`, auto-clear on modification)
- [x] Add completion styles to Theme (`ghostText`, `completionItem`, `completionSelected`, `completionDesc`)
- [x] Design and implement completion popup UI (`CompletionPopup.cpp` - bordered list with scroll indicators)
- [x] Integrate completion with Prompt (Tab/Ctrl+Space triggers, menu navigation, ghost text rendering)
- [x] Add comprehensive completion tests (`Completer_test.cpp`, `CompletionPopup_test.cpp` - 35 tests)

**Implementation Notes:**
- Core completion types (`CompletionItem`, `CompletionProvider`, `Completer`, `SmartCaseMatch`, `FuzzyMatch`) in `src/tui/completer/` as pure TUI model
- Shell-specific providers reorganized into `src/shell/CompletionProviders/` subdirectory for cleaner structure
- Smart case matching: lowercase patterns match case-insensitively; patterns with uppercase match case-sensitively (like Vim's smartcase)
- Fuzzy matching: Typing `ds` matches `Downloads` and `Documents` (matches non-contiguous characters `d...s`); prefix matches scored higher than fuzzy; fuzzy matches have highlighted match positions in completion menu (using `completionMatch` theme style)
- Score bonuses via `SmartCaseConfig`: exact matches get +50, case-exact prefixes get +25 (configurable)
- Score bonuses via `FuzzyConfig`: prefix matches get +50 bonus over fuzzy; quality threshold 20% minimum
- `CompletionContextType` enum: Command, Argument, FilePath, Variable, VariableBrace, Redirect, Option, Unknown
- Ghost text uses SGR 2 (dim) for visual distinction from actual input
- `CompletionPopup` is a proper TUI widget with `show()`/`hide()`/`updateItems()` visibility management, `processEvent()` returning `CompletionAction` enum (Changed, Accepted, Dismissed), and `render()` using relative cursor positioning
- Unhandled keys cause popup to dismiss and pass through to parent (removed `None` action)
- Visibility state is properly synced between `CompletionPopup` and `Component` base class
- Dynamic filtering: typing while popup is visible filters the list in real-time; `updateItems()` preserves selection when the selected item still matches, otherwise selects best match; auto-closes on 0 matches
- Popup positioning: In inline mode (primary screen), always renders below cursor - Screen creates space by emitting newlines to use scrollback buffer; in fullscreen/fixed mode, renders above cursor when not enough space below (< 3 rows)
- Completion menu appears below cursor with Up/Down/Ctrl+J/Ctrl+K/Tab/Shift+Tab navigation, Enter to accept, Escape to dismiss
- Single completion matches are inserted directly without showing menu
- `Environment` class extracted to `Environment.hpp` for cleaner dependency management
- Shell class creates `Completer` with environment and history, connects to Prompt via `setCompleter()`
- Executed commands are added to both prompt history (Up/Down recall) and completion history (suggestions)
- Test utilities in `src/tui/TestHelpers.hpp` for rendering verification (`canvasToString()`, `renderPopup()`, etc.)
- 39 completion-related tests covering Completer, CompletionPopup, and updateItems functionality

### Phase 2.3.5: TUI Renderer Architecture

**Status:** Complete

**Dependency:** Phase 2.1

**Rationale:** The original TUI rendering used direct terminal output without central coordination,
causing issues like `CompletionPopup::hide()` not clearing the screen. The new architecture provides
buffer-based immediate-mode rendering with diff-based terminal updates, proper component hierarchy,
and focus group management.

**Tasks:**
- [x] Create geometry types (`Rect`, `Point`, `Size` in `Rect.hpp`)
- [x] Create cell buffer (`Cell`, `Buffer` types)
- [x] Create drawing context (`Canvas` - bounded view into buffer)
- [x] Create component base class (`Component` with event bubbling, focus, hierarchy)
- [x] Create screen coordinator (`Screen` - manages component tree, rendering, events)
- [x] Implement viewport modes (Fullscreen, Inline with terminal scrolling, Fixed)
- [x] Implement focus groups for multi-focus support (shell + AI overlay)
- [x] Add render mode option (`RenderMode::Diff` vs `RenderMode::Full` for benchmarking)
- [x] Migrate `InputField` to `Component` (inherits from Component, implements render(Canvas&), onEvent())
- [x] Migrate `CompletionPopup` to `Component` (inherits from Component, implements render(Canvas&), onEvent())
- [x] Migrate `List` to `Component` (inherits from Component, implements render(Canvas&), onEvent())
- [x] Migrate `SelectDialog`, `ConfirmDialog`, `InputDialog` to `Component`
- [x] Migrate `StatusBar` to `Component`
- [x] Note: `Box` remains as a utility class (Canvas already has drawBox() method)
- [x] Update `Prompt` to use `Screen` coordinator (Phase 10)
  - Created `PromptComponent` class with styled prompt rendering (left bar, background, colors)
  - Integrated `Screen` with `Inline` viewport mode in `Prompt`
  - Uses Canvas-based rendering through the component tree
- [x] Remove old TerminalOutput-based rendering code (Phase 11)
  - Removed deprecated `render(TerminalOutput&, ...)` methods from all widget headers and implementations
  - Removed unused helper methods (`renderBackground`, `calculateBounds`)
  - Updated includes to use `Theme.hpp` instead of `TerminalOutput.hpp` in widget headers
- [x] Add comprehensive unit tests for new rendering system (Phase 12)
  - Added 65 tests in `Renderer_test.cpp` covering:
    - `Point`: construction, equality, offset
    - `Size`: construction, empty, area
    - `Rect`: construction, factory methods, accessors, contains, intersects, offset, inset, expand, intersect, unite
    - `Cell`: equality, reset, continuation, wide character detection
    - `Buffer`: construction, resize, cell access, putString, fill, clear, cursor state
    - `Canvas`: coordinate translation, clipping, drawing operations, subcanvas

**Implementation Notes:**
- Buffer-based immediate mode: Components render to a `Buffer` via `Canvas`, then `Screen` diffs and flushes
- Component hierarchy: Parent-child relationships with z-index ordering for overlays
- Event bubbling: Events propagate from target up through ancestors until handled
- Focus groups: Multiple independent focus contexts (e.g., "prompt", "ai-chat", "overlay")
- Inline viewport: Shell renders at cursor position, grows downward by emitting newlines
- Widget lifecycle: Persistent tree with explicit `addChild()`/`removeChild()`
- `InputField::Model`: Nested class pattern separates logic from rendering, enables unit testing
- Synchronized output: `Screen::flush()` uses DEC mode 2026 (`SyncGuard`) to batch terminal updates and prevent visual tearing
- Inline cursor management: `flushInline()` properly handles content height changes - moves up by `newLines` (not `contentHeight`) after emitting newlines, and clears excess rows when content shrinks to avoid visual artifacts
- Kitty unscroll extension (WIP): Infrastructure added for `CSI Ps + T` to restore scrollback content. Detected via XTVERSION query. Supported terminals: Kitty, Contour, mintty. Configurable via `UnscrollMode::Auto|Enabled|Disabled`. Currently disabled for inline mode because the sequence shifts the entire screen, which doesn't work well when rendering at the bottom. Needs scroll region approach for proper implementation.
- Unit tests for cursor movement calculations in `Screen_test.cpp` (13 test cases)

**Architecture:**
```
Screen (coordinator)
  ├── owns Buffer (current + previous for diff)
  ├── owns RootComponent (implicit container)
  ├── manages focus groups
  └── dispatches events with bubbling

Canvas (drawing context)
  └── bounded view into Buffer with coordinate translation

Component (base class)
  ├── render(Canvas&) - pure virtual
  ├── onEvent(InputEvent&) - with EventResult for bubbling
  ├── parent/children hierarchy
  ├── visibility, area, z-index
  └── focus group membership
```

### Phase 2.4: Syntax Highlighting

**Dependency:** Phase 2.1

**Tasks:**
- [ ] Design syntax highlighting architecture
- [ ] Implement real-time tokenization
- [ ] Implement semantic highlighting (valid vs invalid commands)
- [ ] Implement configurable color schemes
- [ ] Add highlighting tests

### Phase 2.5: Tooltips and Help

**Dependency:** Phase 2.2 (mouse), Phase 2.3 (completion data), Phase 5.3 (LSP as shared backend)

**Tasks:**
- [ ] Implement mouse-hover tooltip display
- [ ] Implement inline help for commands
- [ ] Implement error tooltips with suggestions
- [ ] Integrate with man pages for command help
- [ ] Consume LSP hover/diagnostics capabilities via in-process API for consistent behavior
  between the interactive shell and external editors

### Phase 2.6: Customizable Prompt

**Dependency:** Phase 2.4 (highlighting for prompt elements)

**Tasks:**
- [ ] Design prompt configuration format
- [ ] Implement prompt segment system
- [ ] Implement common segments (cwd, git, time, exit code)
- [ ] Implement VT420 host-writable status line integration
- [ ] Support OSC-8 hyperlinks in prompts
- [ ] Add prompt configuration tests

---

## Milestone 3: AI Integration

**Priority:** High
**Rationale:** Enables natural language interaction and intelligent assistance.

### Phase 3.1: Provider Abstraction

**Dependency:** None (can develop in parallel with Milestone 1)

**Tasks:**
- [ ] Design AI provider interface
- [ ] Implement local LLM backend (llama.cpp, ollama)
- [ ] Implement Claude API backend
- [ ] Implement OpenAI API backend
- [ ] Implement provider configuration and selection
- [ ] Handle API keys securely
- [ ] Add provider abstraction tests

### Phase 3.2: Natural Language Commands

**Dependency:** Phase 3.1, Milestone 1 (need full language to generate)

**Tasks:**
- [ ] Implement natural language to shell command translation
- [ ] Implement command explanation ("what does this do?")
- [ ] Implement command suggestion based on intent
- [ ] Design safe execution confirmation flow
- [ ] Add NL command tests

### Phase 3.3: Intelligent Assistance

**Dependency:** Phase 3.1, Phase 2.3 (completion)

**Tasks:**
- [ ] Implement AI-powered command completion
- [ ] Implement error recovery suggestions
- [ ] Implement context-aware help
- [ ] Implement learning from user corrections
- [ ] Add assistance tests

### Phase 3.4: Context Awareness

**Dependency:** Phase 3.1, Milestone 1

**Tasks:**
- [ ] Implement working directory context
- [ ] Implement command history context
- [ ] Implement project detection (git, package.json, etc.)
- [ ] Implement environment-aware suggestions
- [ ] Add context tests

---

## Milestone 4: Windows Support

**Priority:** High (must-have for 1.0)
**Rationale:** Required for broad adoption; must be developed in parallel, not as an afterthought.

### Phase 4.1: Build System

**Dependency:** Milestone 0.2 (platform abstraction design)

**Tasks:**
- [ ] Add Windows CMake preset
- [ ] Configure MSVC and Clang-cl support
- [ ] Set up Windows CI pipeline
- [ ] Handle Windows-specific dependencies

### Phase 4.2: Platform Implementation

**Dependency:** Phase 4.1, Milestone 0.2 (abstraction interface ✅)

**Current State:** Platform abstraction interfaces complete with stub implementations
(`WindowsPipe.cpp`, `WindowsTTY.cpp`, `WindowsProcess.cpp`). Stubs return `NotImplemented` errors.

**Design Decision:** Endo prefers forward slashes (`/`) as path separators on all platforms, including Windows.
Windows APIs accept forward slashes, and this consistency simplifies auto-completion, path manipulation,
and user muscle memory. Backslashes remain valid in user input but are normalized internally.

**Tasks:**
- [ ] Implement CreateProcess-based execution (replace `WindowsProcess.cpp` stubs)
- [ ] Implement Windows pipe handling (replace `WindowsPipe.cpp` stubs)
- [ ] Implement ConPTY integration for terminal (replace `WindowsTTY.cpp` stubs)
- [ ] Implement Windows console input handling
- [ ] Handle Windows path separators (prefer forward slashes `/` over backslashes `\`)
- [ ] Handle drive letters in paths (e.g., `C:/Users/...`)
- [ ] Implement PATHEXT handling for executables
- [ ] Add Windows-specific tests

### Phase 4.3: Windows-Specific Features

**Dependency:** Phase 4.2

**Tasks:**
- [ ] Implement PowerShell interoperability
- [ ] Implement CMD compatibility mode (optional)
- [ ] Handle Windows environment variables (case-insensitive)
- [ ] Support UNC paths
- [ ] Add Windows feature tests

---

## Milestone 5: Developer Tools

**Priority:** Medium
**Rationale:** Enables debugging and profiling of shell scripts; differentiator for power users.

### Phase 5.1: Debug Adapter Protocol (DAP) Server

**Dependency:** Milestone 1 (complete language), Phase 1.6 (functions)

**Tasks:**
- [ ] Implement DAP server protocol handling, asseccsible via CLI `endo --dap`
- [ ] Implement breakpoint support
- [ ] Implement step execution (into, over, out)
- [ ] Implement variable inspection
- [ ] Implement call stack display
- [ ] Integrate with VS Code DAP extension
- [ ] Add DAP tests

### Phase 5.2: Profiling and Tracing

**Dependency:** Milestone 1

**Tasks:**
- [ ] Implement execution timing
- [ ] Implement command frequency analysis
- [ ] Implement trace output mode
- [ ] Implement performance bottleneck detection
- [ ] Add profiling tests

### Phase 5.3: Language Server Protocol (LSP)

**Dependency:** Milestone 1 (complete language), Phase 2.4 (syntax highlighting can share tokenizer)

**Rationale:** Provides IDE-grade editing support for Endo shell scripts in external editors
(VS Code, Neovim, Helix, Emacs, etc.). Reuses the existing lexer, parser, and AST infrastructure
to deliver rich language intelligence outside the interactive shell.

**Tasks:**
- [ ] Implement LSP server transport (stdio, accessible via `endo --lsp`)
- [ ] Implement `initialize`/`shutdown`/`exit` lifecycle
- [ ] Implement `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didClose` synchronization
- [ ] Implement `textDocument/publishDiagnostics` (syntax errors, undefined variables, unknown commands)
- [ ] Implement `textDocument/completion` (commands, file paths, variables, builtins, options)
- [ ] Implement `textDocument/hover` (command help via man pages, builtin documentation, variable values)
- [ ] Implement `textDocument/definition` (go-to-definition for functions and variable assignments)
- [ ] Implement `textDocument/references` (find all references to a function or variable)
- [ ] Implement `textDocument/documentSymbol` (outline of functions, aliases, exported variables)
- [ ] Implement `textDocument/signatureHelp` (parameter hints for functions)
- [ ] Implement `textDocument/formatting` and `textDocument/rangeFormatting`
- [ ] Implement `textDocument/rename` (rename function or variable across script)
- [ ] Implement `textDocument/semanticTokens` (semantic highlighting: commands, builtins, variables, strings, operators)
- [ ] Implement `textDocument/codeAction` (quick fixes for common errors, e.g. missing quotes, unset variables)
- [ ] Create VS Code extension with language registration for `.endo` and `.sh` files
- [ ] Add LSP server tests (protocol conformance and language feature tests)

**Implementation Notes:**
- Reuse existing `Lexer` and `Parser` for tokenization and AST construction; run in incremental mode
  for fast re-parsing on edits
- Diagnostics should include structured error context from Milestone 0.3's error hierarchy
- Completion provider should share the abstraction from Phase 2.3 where possible
- Semantic tokens map to the same token categories as Phase 2.4's syntax highlighting
- The LSP server runs as a separate process (`endo --lsp`), similar to the DAP server (`endo --dap`)
- Consider using JSON-RPC library or implementing a lightweight handler on top of `std::iostream`
- The LSP's hover, completion, and diagnostics capabilities can be consumed internally by the
  interactive shell (Phase 2.5 Tooltips, Phase 2.3 Completion) via in-process API calls, avoiding
  the need for duplicate logic between the interactive editor and external editor support

---

## Feature Dependency Graph

```
Milestone 0: Foundation
├── 0.1 UTF-8 Completion
├── 0.2 Platform Abstraction ──────────────────────────────┐
└── 0.3 Error Handling                                     │
                                                           │
Milestone 1: Core Language                                 │
├── 1.1 Variables                                          │
│   └── 1.2 Redirects                                      │
│       └── 1.4 Substitution                               │
│           └── 1.5 Expansions                             │
├── 1.3 Logical Operators                                  │
├── 1.6 Control Flow                                       │
└── 1.7 Job Management ────────────────────────────────────┤
                                                           │
Milestone 2: Terminal UX                                   │
├── 2.1 Rich Text Editor                                   │
│   ├── 2.2 Mouse Integration                              │
│   │   └── 2.5 Tooltips                                   │
│   ├── 2.3 Completion ────────────────────────────────────┤
│   └── 2.4 Syntax Highlighting                            │
│       └── 2.6 Customizable Prompt                        │
                                                           │
Milestone 3: AI Integration                                │
├── 3.1 Provider Abstraction (parallel development OK)     │
│   ├── 3.2 Natural Language Commands                      │
│   ├── 3.3 Intelligent Assistance                         │
│   └── 3.4 Context Awareness                              │
                                                           │
Milestone 4: Windows Support                               │
├── 4.1 Build System ◄─────────────────────────────────────┘
│   └── 4.2 Platform Implementation
│       └── 4.3 Windows-Specific Features

Milestone 5: Developer Tools
├── 5.1 DAP Server
├── 5.2 Profiling
└── 5.3 LSP Server ◄──── reuses Lexer/Parser/AST
    └── feeds into 2.5 Tooltips, 2.3 Completion (in-process API)
```

---

## Risk Assessment

### High Risk

| Risk | Mitigation |
|------|------------|
| Platform abstraction complexity | Start abstraction design early; test both platforms continuously |
| CoreVM limitations for shell semantics | Document limitations; consider VM extensions if needed |
| AI provider API changes | Abstract providers well; version lock API clients |

### Medium Risk

| Risk | Mitigation |
|------|------------|
| Bash compatibility gaps | Document intentional differences; provide migration guide |
| Performance with large command histories | Implement lazy loading and pagination |
| ConPTY quirks on Windows | Test on multiple Windows versions; maintain fallback |

### Low Risk

| Risk | Mitigation |
|------|------------|
| UTF-8 edge cases | Use well-tested Unicode libraries; comprehensive test suite |
| DAP protocol complexity | Reference existing implementations; incremental feature support |
| LSP protocol surface area | Implement incrementally; start with diagnostics and completion, add features progressively |

---

## Success Criteria for 1.0 Release

- [ ] All Milestone 0, 1, 2, and 4 tasks complete
- [ ] Milestone 3 Phase 3.1 and 3.2 complete (basic AI integration)
- [ ] Passes comprehensive test suite on Linux and Windows
- [ ] Documentation complete (user guide, configuration reference)
- [ ] Performance acceptable for interactive use (< 50ms prompt latency)
- [ ] No critical or high-severity bugs

---

## Contributing

Contributions are welcome. When working on a feature:

1. Check this roadmap for dependencies - ensure prerequisites are complete
2. Create an issue referencing the roadmap task
3. Follow the coding guidelines in `CLAUDE.md`
4. Add tests for new functionality
5. Update this roadmap when tasks are completed

---

*This roadmap is a living document. Updates occur as development progresses and priorities evolve.*
