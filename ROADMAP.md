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
| Builtins: `exit`, `true`, `false`, `read`, `cd`, `set`, `unset`, `export` | ✅ |
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

**Status:** Complete (except `Ctrl+Z` suspend handling - deferred to terminal integration)

**Dependency:** Platform abstraction (Milestone 0.2)

**Tasks:**
- [x] Implement background execution `&`
- [x] Implement `jobs` builtin
- [x] Implement `fg` builtin
- [x] Implement `bg` builtin
- [ ] Implement `Ctrl+Z` suspend handling → Deferred: requires terminal/TTY signal integration
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
- Uses `signalfd` on Linux for race-free SIGCHLD handling
- Falls back to traditional signal handlers on macOS/BSD
- Process groups are properly managed for job control
- Ctrl+Z (SIGTSTP) handling deferred until terminal integration is complete

---

## Milestone 2: Terminal UX

**Priority:** High
**Rationale:** Differentiates Endo from other shells; delivers on the "IDE-like" promise.

### Phase 2.1: Rich Text Editor Foundation

**Dependency:** Milestone 1 complete (need full language for practical editing)

**Tasks:**
- [ ] Design grid-based text rendering architecture
- [ ] Implement multi-line input support
- [ ] Implement cursor movement (word, line, document)
- [ ] Implement text selection model
- [ ] Implement undo/redo history
- [ ] Implement clipboard integration (OSC 52)
- [ ] Add editor unit tests

### Phase 2.2: Mouse Integration

**Dependency:** Phase 2.1

**Tasks:**
- [ ] Implement passive mouse tracking VT extension support
- [ ] Implement click-to-position cursor
- [ ] Implement click-and-drag selection
- [ ] Implement double-click word selection
- [ ] Implement triple-click line selection
- [ ] Add mouse interaction tests

### Phase 2.3: Completion and Suggestions

**Dependency:** Phase 2.1, Milestone 1 (need language features to complete)

**Tasks:**
- [ ] Design completion provider interface
- [ ] Implement command name completion
- [ ] Implement file path completion
- [ ] Implement variable name completion
- [ ] Implement option/flag completion (with help text)
- [ ] Implement history-based suggestions
- [ ] Design and implement completion popup UI
- [ ] Add completion tests

### Phase 2.4: Syntax Highlighting

**Dependency:** Phase 2.1

**Tasks:**
- [ ] Design syntax highlighting architecture
- [ ] Implement real-time tokenization
- [ ] Implement semantic highlighting (valid vs invalid commands)
- [ ] Implement configurable color schemes
- [ ] Add highlighting tests

### Phase 2.5: Tooltips and Help

**Dependency:** Phase 2.2 (mouse), Phase 2.3 (completion data)

**Tasks:**
- [ ] Implement mouse-hover tooltip display
- [ ] Implement inline help for commands
- [ ] Implement error tooltips with suggestions
- [ ] Integrate with man pages for command help

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
- [ ] Implement DAP server protocol handling, asseccsible via CLI `endo --dap` and `endo --dap-tcp <PORT>`
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
└── 5.2 Profiling
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
