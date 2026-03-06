# Debug Adapter Protocol (DAP) Roadmap

> Implementation roadmap for full [DAP](https://microsoft.github.io/debug-adapter-protocol/) support in Endo,
> enabling script debugging from VS Code, Neovim, and any DAP-compatible editor.

## Existing Infrastructure

Endo already has strong foundations for DAP integration:

| Component | Status | Location |
|-----------|--------|----------|
| Source location tracking | Complete | `src/CoreVM/SourceLocation.hpp` |
| Bytecode → source mapping | Complete | `Function::locationOf()` in `src/CoreVM/vm/Function.cpp` |
| VM suspend/resume | Complete | `Runner::suspend()`, `resume()` in `src/CoreVM/vm/Runner.hpp` |
| Call stack frames | Complete | `Runner::CallFrame { ip, function, fp }` |
| Per-instruction trace hook | Complete | `TraceLogger` callback in `Runner` |
| JSON transport (Content-Length framing) | Complete | `src/editor-protocol/JsonTransport.hpp` (shared by LSP and DAP) |
| Document store (URI → text mapping) | Complete | `src/editor-protocol/DocumentStore.hpp` (shared by LSP and DAP) |
| Source types (Position, Range, Location) | Complete | `src/editor-protocol/EditorTypes.hpp` (shared by LSP and DAP) |
| Stub runtime initialization | Complete | `src/editor-protocol/StubRuntime.hpp` (shared by LSP and DAP) |
| Test utilities (in-memory I/O) | Complete | `src/editor-protocol/TestHelpers.hpp` (shared by LSP and DAP) |
| Type registry & value formatting | Complete | `TypeRegistry`, `valueToString()` |
| CLI flag pattern | Complete | `--lsp` in `src/shell/main.cpp` |

---

## Phase 0: Shared Library — `editor-protocol`

**Goal**: Extract protocol-generic code from `src/lsp/` into a shared `src/editor-protocol/` library that both LSP and DAP link against. This avoids code duplication and establishes a clean dependency structure.

**Components to extract**:

| Component | Source | What's Generic |
|-----------|--------|----------------|
| JSON transport | `src/lsp/JsonRpc.{hpp,cpp}` | `readMessage()`, `writeMessage()` — Content-Length framed JSON I/O (identical wire format for LSP and DAP) |
| Document store | `src/lsp/DocumentStore.{hpp,cpp}` | URI-to-text mapping with versioning — DAP needs source tracking for `handleSource()` |
| Source types | `src/lsp/LspTypes.hpp` | `Position`, `Range`, `Location`, `TextEdit`, `toRange()` — common editor/IDE concepts |
| Stub runtime | `src/lsp/StubRuntime.hpp` | Runtime initialization wrapper — needed by any tool analyzing Endo source |
| Test helpers | `src/lsp/LspServer_test.cpp` | `makeRpcMessage()`, `readAllMessages()`, in-memory I/O pattern |

**Tasks**:

- [x] Create `src/editor-protocol/` directory with `CMakeLists.txt` (static library `endo-editor-protocol`, link to `nlohmann_json`, `endo`)
- [x] Move `JsonRpc.{hpp,cpp}` → `src/editor-protocol/JsonTransport.{hpp,cpp}` (rename to be protocol-neutral; keep `readMessage()`, `writeMessage()`, `ErrorCode` enum)
- [x] Extract generic types into `src/editor-protocol/EditorTypes.hpp`: `Position`, `Range`, `Location`, `TextEdit`, `WorkspaceEdit`, `toRange()` (with nlohmann::json serialization)
- [x] Move `DocumentStore.{hpp,cpp}` → `src/editor-protocol/DocumentStore.{hpp,cpp}` (no changes needed)
- [x] Move `StubRuntime.hpp` → `src/editor-protocol/StubRuntime.hpp`
- [x] Create `src/editor-protocol/TestHelpers.hpp` with `makeRpcMessage()`, `sendRequest()`, `sendNotification()`, `readAllMessages()`
- [x] Add `add_subdirectory(editor-protocol)` to `src/CMakeLists.txt` (before `lsp` and `dap`)
- [x] Update `src/lsp/CMakeLists.txt`: replace moved sources with dependency on `endo-editor-protocol`; add `#include` path adjustments
- [x] Update all `#include` paths in `src/lsp/` to reference `editor-protocol/` headers
- [x] Keep LSP-specific types (`Diagnostic`, `Hover`, `SemanticTokens`, `CompletionItem`, etc.) in `src/lsp/LspTypes.hpp`
- [x] Verify LSP still builds and all LSP tests pass after extraction
- [x] Update `src/lsp/LspServer_test.cpp` to use shared `TestHelpers` instead of local helpers

**Resulting dependency graph**:
```
endo-editor-protocol  (shared: transport, types, document store)
    ├── endo-lsp      (LSP-specific: providers, LspServer, LspTypes)
    └── endo-dap      (DAP-specific: DebugSession, DapServer, DapTypes)
```

---

## Phase 1: Foundation — Protocol Types, Session Lifecycle

**Goal**: Establish the DAP message types and session lifecycle on top of `editor-protocol`. Fully testable in isolation.

**Capabilities after this phase**: `supportsConfigurationDoneRequest`

- [x] Create `src/dap/` directory with `CMakeLists.txt` (static library `endo-dap`, test executable `test-endo-dap`; link to `endo-editor-protocol`, `endo`, `nlohmann_json`, `CoreVM`)
- [x] Add `add_subdirectory(dap)` to `src/CMakeLists.txt` (inside `if(NOT EMSCRIPTEN)` block)
- [x] Define DAP message structures in `src/dap/DapTypes.hpp`:
  - `DapRequest` (seq, command, arguments)
  - `DapResponse` (request_seq, success, command, body, message)
  - `DapEvent` (seq, event, body)
  - `Capabilities` struct with all capability flags as `std::optional<bool>`
  - `InitializeRequestArguments` (clientID, clientName, linesStartAt1, columnsStartAt1, pathFormat)
- [x] Implement `src/dap/DapServer.{hpp,cpp}`:
  - Constructor taking `std::istream&, std::ostream&`
  - `int run()` — message loop
  - `dispatch(json const& message)` — route by `command` field
  - `sendResponse(int requestSeq, string command, json body)`
  - `sendErrorResponse(int requestSeq, string command, string message)`
  - `sendEvent(string event, json body)` with monotonic sequence numbering
- [x] Implement `handleInitialize()` — return capabilities, send `initialized` event
- [x] Implement `handleConfigurationDone()` — acknowledge, mark configuration complete
- [x] Implement `handleDisconnect()` — with `terminateDebuggee` support
- [x] Implement `handleTerminate()` — stop the debuggee
- [x] Implement lifecycle state machine (`_initialized`, `_configurationDone`, `_terminated` flags)
- [x] Implement `handleLaunch()` — accept `program` (script path), `args`, `stopOnEntry`, `noDebug`; parse and compile the script; defer execution until `configurationDone`
- [x] Send `terminated` event when execution completes
- [x] Send `exited` event with exit code after termination
- [x] Add `--dap` flag to `src/shell/main.cpp`, launching `DapServer{stdin, stdout}.run()`
- [x] Unit tests: message framing round-trip, initialize/configurationDone/disconnect lifecycle, launch with valid/invalid script, sequence numbering
- [x] Integration test: full session (initialize → launch → configurationDone → terminated → exited → disconnect)

---

## Phase 2: Breakpoints

**Goal**: Implement source and function breakpoints using the existing `TraceLogger` hook.

**Capabilities after this phase**: source breakpoints, `supportsFunctionBreakpoints`

- [x] Design `src/dap/BreakpointManager.{hpp,cpp}`:
  - Storage: `unordered_map<string /*path*/, vector<ResolvedBreakpoint>>` for source breakpoints
  - Storage: `vector<ResolvedFunctionBreakpoint>` for function breakpoints
  - `struct ResolvedBreakpoint { int id; string source; int line; int column; bool verified; }`
  - `setSourceBreakpoints(path, breakpoints)` — replaces all breakpoints for a file
  - `setFunctionBreakpoints(breakpoints)` — replaces all function breakpoints
  - `shouldStop(filename, line) const` — O(1) fast-path check (packed `unordered_set<uint64_t>`)
  - Monotonic breakpoint ID allocation
- [x] Implement breakpoint resolution: given a requested line, find the closest bytecode instruction via the `Function` location table; mark as `verified = true` if an instruction maps to or near the requested line
- [x] Implement `handleSetBreakpoints()` — extract `source.path` and `breakpoints[]` with `{line, column?, condition?, hitCondition?, logMessage?}`; store condition/hit/logMessage for Phase 5; return `Breakpoint[]` with resolved locations
- [x] Implement `handleSetFunctionBreakpoints()` — match function names against `Program::functionNames()`; resolve to first instruction of named function
- [x] Design `src/dap/DebugSession.{hpp,cpp}` — owns `BreakpointManager`, `Runner*`, compiled `Program`, stop state, and the DAP-aware `TraceLogger`:
  - TraceLogger callback: look up source location via `function->locationOf(ip)`, call `shouldStop(filename, line)`
  - On breakpoint hit: set stop reason, call `runner->suspend()`, emit `stopped` event with `reason: "breakpoint"`, `threadId: 1`, `hitBreakpointIds`
- [x] Wire `DapServer` to `DebugSession` (launch creates session, breakpoint requests delegate)
- [x] Handle stopped state: when VM is suspended, `DapServer::run()` processes requests until VM is resumed
- [x] Implement `handleBreakpointLocations()` — given a source range, return all possible breakpoint locations from the location table
- [x] Tests: breakpoint on valid line resolves; breakpoint on empty line snaps to nearest; function breakpoint by name; run to breakpoint produces `stopped` event; clear breakpoints and verify no stops

---

## Phase 3: Execution Control

**Goal**: Continue, step over/in/out, pause, and `stopOnEntry`.

**Capabilities after this phase**: full execution control

- [x] Implement `handleContinue()` — resume VM via `runner->resume()`, clear step state; emit `continued` event; if VM completes, send `terminated` + `exited`
- [x] Design stepping state in `DebugSession`:
  - `enum class StepMode { None, StepOver, StepIn, StepOut }`
  - Track `_stepStartFrame` (call stack depth) and `_stepStartLine` (source line)
- [x] Implement `handleNext()` (step over) — set `StepMode::StepOver`, record current stack depth and line; in TraceLogger: stop when depth ≤ start depth AND line changed
- [x] Implement `handleStepIn()` — set `StepMode::StepIn`, record current line; in TraceLogger: stop when line changes (naturally enters function calls)
- [x] Implement `handleStepOut()` — set `StepMode::StepOut`, target depth = current depth − 1; in TraceLogger: stop when depth ≤ target
- [x] Implement `handlePause()` — set `_pauseRequested` flag; TraceLogger checks this flag (highest priority, before breakpoints); stop with `reason: "pause"`
- [x] Implement `stopOnEntry` — when `launch` has `stopOnEntry: true`, stop at first instruction of `@main` with `reason: "entry"`
- [x] Emit `continued` event on each resume
- [x] Handle native callbacks: native callbacks don't fire the TraceLogger, so step-over naturally skips them
- [x] Fix BrInstr phantom location table entries causing every-other-line stepping
- [x] Document AST-inlined function behavior: step-in on inlined calls walks through inlined body (different source lines, same call depth)
- [x] Tests: continue from breakpoint to end; step over a line; step into a function; step out of a function; pause during execution; stopOnEntry

---

## Phase 4: Inspection — Stack Traces, Scopes, Variables, Evaluate

**Goal**: Let users see where they are, what variables exist, and what values they hold. This is the highest-value feature set.

**Capabilities after this phase**: `supportsEvaluateForHovers`, `supportsVariableType`

### VM Accessors

- [x] Add public accessors to `Runner`:
  - `std::span<CallFrame const> callStack() const`
  - `size_t framePointer() const`
  - `Function const* currentFunction() const`

### Debug Info Tables (Variable Name Recovery)

At runtime, F# variables are anonymous stack slots — names are lost after compilation. The IR `AllocaInstr` objects still carry names (e.g., `"x"`, `"result"`).

- [x] Extend `Function` (or `ConstantPool`) with a debug info table: `vector<DebugVarInfo>` where `DebugVarInfo = { string name, int allocaIndex, LiteralType type }`
- [x] Populate during `TargetCodeGenerator::generate()` by walking `AllocaInstr` instructions and recording `{name(), allocaIndex, type()}`
- [x] Zero cost when not debugging (table exists but is only read by DAP)

### Stack Traces

- [x] Implement `handleThreads()` — return `[{id: 1, name: "main"}]` (Endo is single-threaded)
- [x] Implement `handleStackTrace()`:
  - Current execution point as frame 0
  - Walk `Runner::callStack()` from top to bottom
  - For each `CallFrame`: `frame.function->name()` + `frame.function->locationOf(frame.ip)` for source location
  - Return `StackFrame[]` with `{id, name, source: {path}, line, column}`

### Scopes & Variables

- [x] Design variable reference scheme:
  - Frame scopes: `variablesReference = (frameId * 1000) + scopeType` (1=locals, 2=globals, 3=captures)
  - Structured values (objects with slots): allocate dynamic references via `VariableReferenceMap`
  - Reset references on each stop
- [x] Implement `handleScopes()` — given `frameId`, return scopes: "Locals" (allocas in function), "Globals" (shell environment), optionally "Captures"
- [x] Implement `handleVariables()`:
  - Resolve `variablesReference` to scope or structured value
  - Locals: iterate debug info table, read stack slots relative to frame pointer, format via `valueToString()`
  - Globals: iterate `_globals`, format as key-value pairs
  - Structured values (List, Tuple, Option, Result, records): use `TypeDescriptor::fields`/`variants` to enumerate slots; set `variablesReference` for expandable children
  - Return `Variable[]` with `{name, value, type, variablesReference}`

### Expression Evaluation

- [x] Implement `handleEvaluate()` for `context: "hover"` and `"watch"`:
  - Simple variable name lookup in current scope's debug info
  - Return `{result, type, variablesReference}` for structured results
- [x] Full REPL expression evaluation: compile and execute expressions with in-scope variable injection

### Tests

- [x] Tests: threads returns 1 thread; stack trace at breakpoint shows correct frames with source locations; scopes show locals; variables show correct names and values for numbers, strings, booleans, floats, tuples, lists, options, results, records; evaluate a variable name; expand compound variable children

---

## Phase 5: Advanced Features

**Goal**: Elevate from functional to professional-grade debugger.

**Capabilities after this phase**: `supportsConditionalBreakpoints`, `supportsHitConditionalBreakpoints`, `supportsLogPoints`, `exceptionBreakpointFilters`, `supportsExceptionInfoRequest`, `supportsDisassembleRequest`, `supportsSteppingGranularity`, `supportsSetVariable`

### Conditional & Hit-Count Breakpoints

- [x] Extend `shouldStop()` to evaluate `condition` expression (parse, compile, execute in current context, check truthiness); cache compiled conditions
- [x] Implement hit count tracking: per-breakpoint counter, `hitCondition` expressions (`">=3"`, `"==5"`)

### Log Points

- [x] When breakpoint has `logMessage`: do not stop; evaluate `{expression}` interpolations; emit `output` event with `category: "console"`; continue

### Exception Breakpoints

- [x] Implement `handleSetExceptionBreakpoints()` with filters: `"runtime-error"` (RuntimeError), `"all"` (any error)
- [x] Hook into `Runner` error paths: before returning `std::unexpected(error)`, check DAP exception breakpoint state; stop with `reason: "exception"`
- [x] Implement `handleExceptionInfo()` — return exception message, type, source location

### Disassembly

- [x] Implement `handleDisassemble()` — given memory reference (`"func:NAME:OFFSET"`), return disassembled bytecode instructions with source locations using existing `CoreVM::disassemble()`

### Stepping Granularity

- [x] Support `granularity` in next/stepIn/stepOut: `"instruction"` (stop every bytecode op), `"line"` (default, stop on source line change), `"statement"` (same as line)

### Variable Mutation

- [x] Implement `handleSetVariable()` — for mutable variables (`let mut`), write new value to stack slot; refuse for immutable bindings

### Full Expression Evaluation

- [x] Implement full `handleEvaluate()` for `context: "repl"`:
  - Parse expression, compile to temporary function, execute in sandboxed Runner sharing globals
  - Capture output and return value
  - Handle errors gracefully (error response, not crash)

### Tests

- [x] Tests: conditional breakpoint stops only when condition met; hit count breakpoint; log point emits output without stopping; exception breakpoint on runtime error; disassemble a function; instruction-level stepping; set mutable variable; REPL evaluate expression

---

## Phase 6: Editor Integration & Tooling

**Goal**: Make the debugger accessible from VS Code, Neovim, and any DAP client.

### VS Code Extension

- [x] Create extension scaffold in `editors/vscode/`:
  - `package.json` with `debuggers` contribution point (`type: "endo"`)
  - Debug adapter descriptor: `type: "executable"`, `command: "endo"`, `args: ["--dap"]`
- [x] Define `launch.json` schema: `program` (required), `args` (optional), `stopOnEntry` (optional, default false), `noDebug` (optional)
- [x] Implement `DebugConfigurationProvider` — auto-detect current `.endo` file as `program`
- [x] Add `launch.json` snippets: "Launch Endo Script", "Launch Current File", "Launch with Arguments"
- [x] Extension keywords and marketplace metadata

### Neovim (nvim-dap)

- [x] Create example configuration in `editors/neovim/dap.lua`
- [x] Add filetype detection for `.endo` files (`vim.filetype.add({ extension = { endo = "endo" } })`)
- [x] Document installation steps for nvim-dap + endo integration (`docs/debugging/neovim.md`)
- [x] Test with nvim-dap: breakpoints, stepping, variable inspection, evaluate

### Output Capture

- [x] Redirect `print`/`println` output as `output` events with `category: "stdout"`
- [x] Redirect compilation errors as `output` events with `category: "stderr"`
- [x] Redirect runtime errors as `output` events with `category: "stderr"`

### Source & Module Requests

- [x] Implement `handleSource()` — return source code for loaded script files
- [x] Implement `handleLoadedSources()` — return all loaded source files

### Protocol Logging

- [x] Add `--log-file=FILE` option for DAP protocol logging (all sent/received messages)

### Documentation

- [x] Write `docs/debugging/` with index.md, vscode.md, neovim.md
- [x] Update `--help` text in `src/shell/main.cpp` to document `--dap` and `--log-file`

### Performance & Polish

- [x] Measure TraceLogger overhead on script execution (target: <100% with no breakpoints)
- [x] End-to-end integration tests (programmatic DAP client simulation)
- [x] Update `ROADMAP.md` Phase 5.1 to reference this document

---

## Phase 7: Protocol Compliance & Polish

**Goal**: Fix protocol compliance issues, add missing events, and implement remaining high-value features for real-world use.

### Protocol Compliance Fixes

- [x] Add `allThreadsStopped` to all `stopped` events (fixes VS Code stack/variable refresh)
- [x] Fix error response body format: add `body.error` with `id` and `format` fields per DAP spec
- [x] Advertise `supportsTerminateRequest` in capabilities
- [x] Handle `terminateDebuggee` in disconnect request

### Missing Events

- [x] Emit `process` event after successful launch (with `name` and `startMethod`)
- [x] Emit `thread` event with `reason: "started"` on execution start
- [x] Emit `thread` event with `reason: "exited"` on termination

### New Features

- [x] `restart` request — destroys session and re-launches with saved launch args
- [x] `completions` request — debug console autocomplete (variables + function names)
- [x] `setInstructionBreakpoints` request — breakpoints by bytecode address
- [x] `cancel` request — no-op handler for synchronous server

### Minor Improvements

- [x] Support `string` type in `setVariable` (allocates via `Runner::newString()`)
- [x] Store `InitializeRequestArguments` for `linesStartAt1`/`columnsStartAt1` awareness

### Tests

- [x] `allThreadsStopped` present in stopped events
- [x] `process` and `thread` events emitted
- [x] Error response body format compliance
- [x] `restart` request re-runs script
- [x] `completions` returns variable/function names
- [x] `setInstructionBreakpoints` stops at bytecode address
- [x] `cancel` returns success
- [x] `setVariable` with string values
- [x] Phase 7 capabilities advertised in initialize response

---

## Phase Dependencies

```
Phase 0: editor-protocol (shared library extraction)
    │
    ├──► LSP (updated to depend on editor-protocol)
    │
    ▼
Phase 1: Foundation
    │
    ▼
Phase 2: Breakpoints
    │
    ▼
Phase 3: Execution Control
    │
    ▼
Phase 4: Inspection ──────► Phase 6: Editor Integration
    │
    ▼
Phase 5: Advanced Features
    │
    ▼
Phase 7: Protocol Compliance & Polish
    │
    ▼
Phase 8: Extended Protocol Features
```

Phase 0 is a prerequisite for Phase 1 (and also updates the existing LSP). Phases 1–4 are sequential. Phases 5 and 6 can proceed in parallel once Phase 4 is complete. Phases 0–4 form the **MVP** (usable debugger with breakpoints, stepping, and variable inspection). Phase 8 adds optional DAP features beyond the core protocol.

---

## Phase 8: Extended Protocol Features

**Goal**: Implement remaining DAP specification features that add value for endo's execution model.

### Attach Support

- [ ] Implement `handleAttach()` — connect to an already-running endo session (e.g., interactive shell) for live debugging
- [ ] Define `attach` request arguments: `processId` or `pipe`/`socket` transport
- [ ] Update VS Code extension with `attach` launch configuration and snippets
- [ ] Update Neovim DAP configuration with attach example
- [ ] Advertise attach support in capabilities
- [ ] Tests: attach to running script, set breakpoints, inspect variables, disconnect without terminating

### Data Breakpoints

- [ ] Implement `handleDataBreakpointInfo()` — given a variable name and frame, return whether data breakpoints are supported for it and a `dataId`
- [ ] Implement `handleSetDataBreakpoints()` — break on variable write/read/access
- [ ] Extend `BreakpointManager` with data breakpoint storage and tracking
- [ ] Hook into `STORE` instruction in TraceLogger to detect variable writes
- [ ] Advertise `supportsDataBreakpoints` in capabilities
- [ ] Tests: data breakpoint on variable write stops execution; clear data breakpoints

### Goto Targets

- [ ] Implement `handleGotoTargets()` — given a source location, return valid goto targets (statement boundaries)
- [ ] Implement `handleGoto()` — move execution to a target location within the current function
- [ ] Validate target is within the same function (reject cross-function jumps)
- [ ] Advertise `supportsGotoTargetsRequest` in capabilities
- [ ] Tests: goto a valid target; reject invalid target; goto within a loop

### Set Expression

- [ ] Implement `handleSetExpression()` — evaluate an expression and assign the result to a variable
- [ ] Reuse `evaluateReplExpression()` infrastructure for the right-hand side
- [ ] Advertise `supportsSetExpression` in capabilities
- [ ] Tests: set variable via expression; error on invalid expression; error on immutable binding

### Module Support

- [ ] Implement `handleModules()` — return loaded source files as modules (script file + any `source`d files)
- [ ] Emit `module` events when new source files are loaded
- [ ] Advertise `supportsModulesRequest` in capabilities
- [ ] Tests: modules request returns loaded scripts; module event on source load

### Tests

- [ ] End-to-end integration tests for all Phase 8 features
- [ ] Update documentation in `docs/debugging/` for new capabilities

---

## Key Architecture Decisions

| Decision | Rationale |
|----------|-----------|
| `editor-protocol` shared library | DAP and LSP share transport, document store, source types, and test utilities — extract once, link from both |
| `TraceLogger` as integration hook | Fires before every instruction — natural breakpoint/step check point |
| Single thread (always `threadId: 1`) | Endo's VM is single-threaded; simplifies everything |
| Debug info table in `ConstantPool` | `AllocaInstr::name()` already carries variable names; zero VM overhead |
| stdio transport (`--dap`) | Standard for both VS Code and nvim-dap; simplest to implement |
| Variable references reset on each stop | References only valid while stopped; avoids stale pointer issues |

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| TraceLogger overhead on every instruction | Slows all script execution | Fast-path boolean check when no breakpoints/steps active |
| Variable names lost after compilation | Unnamed variables in debugger | AllocaInstr names available; debug info table is straightforward |
| Single-threaded message processing | Debugger unresponsive during long runs | Periodic pause-check in TraceLogger every N instructions |
| Full expression evaluation complexity | Scope/state management issues | Phase 4 does simple lookups; full eval deferred to Phase 5 |
