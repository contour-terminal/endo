# Endo LSP Server — Implementation Status & Roadmap

This document tracks the implementation status of Language Server Protocol (LSP 3.17) features
for the Endo language server (`endo --lsp`).

The server reuses the shared `endo-language` library (Lexer, Parser, AST, IDE infrastructure)
and communicates via JSON-RPC 2.0 over stdio.

**Legend:** [x] Implemented | [~] Partial | [ ] Not yet implemented

---

## Tier 1 — Core (High Impact)

Foundation for a useful editing experience. All features here are implemented.

### Lifecycle & Transport

- [x] `initialize` / `initialized` / `shutdown` / `exit`
- [x] JSON-RPC 2.0 transport over stdio (`endo --lsp`)
- [x] Error responses with standard JSON-RPC error codes
- [x] Pre-initialization guard (rejects requests before `initialize`)

### Document Synchronization

- [x] `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didClose`
- [~] Full document sync (`TextDocumentSyncKind=1`)
  - Missing: incremental sync (`TextDocumentSyncKind=2`) — lower priority since Endo files tend to be small

### Diagnostics

- [x] `textDocument/publishDiagnostics`: syntax errors, undefined variables, unknown commands, type-check suggestions
- [x] Diagnostics cleared on `didClose`
- [x] Diagnostics refreshed on `didOpen` and `didChange`

### Hover

- [x] `textDocument/hover`: keywords, constructors, operators, builtins, function signatures, record types, pipeline operators
- [x] Markdown-formatted content via shared `endo::computeHover()` engine

### Go to Definition

- [x] `textDocument/definition`: functions, variables, parameters, pattern bindings
- [x] Scope-aware resolution via `SymbolCollector`

### Find References

- [x] `textDocument/references`: all references to a symbol
- [x] Supports `context.includeDeclaration`

### Signature Help

- [x] `textDocument/signatureHelp`: parameter hints for user-defined functions
- [x] Trigger characters: ` `, `(`
- [x] Active parameter tracking via token-based position analysis

### Completion

- [x] `textDocument/completion`: keywords, builtins, user functions, variables, constructors, record fields, module functions, shell commands
- [x] Trigger characters: `.`, `$`, ` `
- [x] Context-aware: dot-access for records, module-qualified calls (`File.xxx`), shell command output types
- [x] Shared engine via `endo::computeCompletions()`
- [x] `completionItem/resolve`: deferred documentation lookup for builtins and keywords

### Rename

- [x] `textDocument/rename`: scope-aware rename across document
- [x] `textDocument/prepareRename`: identifier range validation before rename

### Document Symbols

- [x] `textDocument/documentSymbol`: hierarchical outline (functions with parameter children, top-level let bindings, record types with field children, union types with variant children, property bindings, finer `SymbolKind` usage)
- [x] Nested symbols (let-in bindings inside function bodies reported as children)

### Formatting

- [x] `textDocument/formatting`: whole document formatting via `SourceFormatter::format()`
- [x] `textDocument/rangeFormatting`: format selected range only (line-level granularity)
- [x] `textDocument/onTypeFormatting`: auto-indent on newline after block openers (`=`, `->`, `then`, `do`, `with`), pipe alignment on `|`

### Semantic Tokens

- [x] `textDocument/semanticTokens/full`: 9 token types (`keyword`, `function`, `variable`, `number`, `string`, `operator`, `enumMember`, `comment`, `type`), 2 modifiers (`declaration`, `modification`)
- [x] `textDocument/semanticTokens/full/delta`: incremental token updates via `resultId` diffing
- [x] `textDocument/semanticTokens/range`: tokenize only the requested line range

---

## Tier 2 — Enhanced Editing

High-value features that significantly improve the day-to-day editing experience.

### Code Actions & Quick Fixes

- [x] `textDocument/codeAction`: quick fixes derived from diagnostic suggestions
  - [x] "Did you mean?" for misspelled function/variable names (auto-replaces with TextEdit)
  - [x] Informational suggestions displayed as quickfix actions
  - [x] Diagnostic `data` field round-trips raw suggestions for code action generation
  - Reuse: `DiagnosticsCollector` already produces `suggestions` vectors per diagnostic
- [x] `codeAction/resolve`: returns action as-is (all actions are eagerly computed; infrastructure for future deferred actions)

### Document Highlight

- [x] `textDocument/documentHighlight`: highlight all occurrences of the symbol under cursor
  - Uses `SymbolCollector::findHighlights()` with `DocumentHighlightKind::Read`/`Write` distinction

### Folding Ranges

- [x] `textDocument/foldingRange`: collapsible regions
  - [x] Function bodies (multi-line `let f x = ...`)
  - [x] Match expressions (from `match` to last `|` arm)
  - [x] If-then-else blocks, for/while loop bodies
  - [x] Block scopes `{ ... }`, seq/list comprehension bodies, record expressions
  - [x] Multi-line comments `(* ... *)` as comment fold kind
  - [x] Type definitions (record, union)
  - Implementation: AST walk over multi-line nodes + lexer comment collection

### Selection Range

- [x] `textDocument/selectionRange`: smart expand/shrink selection
  - [x] Hierarchy: identifier → expression → statement → block → function body → top-level
  - [x] Supports multiple cursor positions per request
  - Implementation: AST walk finding all nodes containing cursor position, sorted by range size

### Inlay Hints

- [x] `textDocument/inlayHint`: inline type annotations
  - [x] Inferred parameter types for untyped function parameters (via Hindley-Milner type inference)
  - [x] Inferred return types for function definitions
  - [x] Variable types from `let` bindings
  - [x] Pipeline intermediate types (`data |> map f |> filter g`) via `InferenceResult.exprTypes`
  - Reuse: type inference (Algorithm W) already runs as a pre-pass before IR generation
- [x] `inlayHint/resolve`: populates tooltip with expanded type information

---

## Tier 3 — Advanced Navigation

Features for navigating larger codebases and understanding code structure.

### Call Hierarchy

- [x] `textDocument/prepareCallHierarchy`: find function at cursor position
- [x] `callHierarchy/incomingCalls`: who calls this function (grouped by caller)
- [x] `callHierarchy/outgoingCalls`: what does this function call (grouped by callee)
  - Uses `SymbolCollector` call relation tracking via `ApplicationExpr` analysis

### Workspace Symbol

- [x] `workspace/symbol`: search symbols across all open documents
  - Aggregates `collectSymbols()` results across all documents in `DocumentStore`
  - Case-insensitive substring matching on symbol names

### Go to Type Definition

- [x] `textDocument/typeDefinition`: navigate from variable to its type definition
  - From a variable with a type annotation matching a user-defined record/union → jumps to `type` definition

### Document Link

- [x] `textDocument/documentLink`: clickable links in source
  - `source "file"` shell commands
  - `File.open "path"` / `open "path"` string arguments
- [x] `documentLink/resolve`: resolves deferred targets from link data

---

## Tier 4 — IDE Polish

Features that make the experience feel premium.

### Code Lens

- [x] `textDocument/codeLens`: inline annotations above function definitions
  - Each function definition gets a code lens with deferred reference count
- [x] `codeLens/resolve`: resolves reference count (e.g., "2 references", "0 references")

### Window Notifications

- [x] `window/showMessage`: server-to-client informational messages
- [x] `window/logMessage`: structured log output
- [ ] `window/showMessageRequest`: messages with response options (requires server-to-client request support)

### Work Done Progress

- [x] `window/workDoneProgress/create` + `$/progress`: RAII progress reporting for long operations
- [x] `window/workDoneProgress/cancel`: client notification to cancel (accepted, no-op for now)

### Inline Value

- [x] `textDocument/inlineValue`: variable lookup entries for DAP integration (skeleton)
  - Returns `InlineValueVariableLookup` entries for variable references in the requested range

---

## Tier 5 — Protocol Completeness

Features included for completeness. Most have limited relevance to Endo.

### Low Relevance for Endo

- [ ] `textDocument/declaration`: Endo has no declaration/definition split (definition IS declaration)
- [ ] `textDocument/implementation`: no interfaces or abstract types
- [ ] `textDocument/prepareTypeHierarchy` + `typeHierarchy/supertypes` + `typeHierarchy/subtypes`: structural type system, no inheritance
- [ ] `textDocument/documentColor` + `textDocument/colorPresentation`: no color literals
- [ ] `textDocument/moniker`: cross-package symbol identification — not applicable until module system exists
- [ ] `textDocument/linkedEditingRange`: rename already covers this use case
- [ ] Notebook document support (`notebookDocument/*`): not applicable

### Workspace Features (future, when multi-file support matures)

- [ ] `workspace/configuration`: read editor settings
- [ ] `workspace/workspaceFolders` + `workspace/didChangeWorkspaceFolders`: multi-root workspace
- [ ] `workspace/didChangeWatchedFiles`: react to external file changes
- [ ] `workspace/didChangeConfiguration`: react to settings changes
- [ ] `workspace/executeCommand`: custom commands (run script, format all)
- [ ] `workspace/applyEdit`: server-initiated workspace edits
- [ ] `workspace/codeLens/refresh`: trigger code lens refresh from server
- [ ] `workspace/inlineValue/refresh`: trigger inline value refresh from server
- [ ] `workspace/semanticTokens/refresh`: trigger semantic tokens refresh from server
- [ ] `workspace/inlayHint/refresh`: trigger inlay hint refresh from server
- [ ] `workspace/diagnostic/refresh`: trigger diagnostic refresh from server
- [ ] `workspace/foldingRange/refresh`: trigger folding range refresh from server
- [ ] File operations: `willCreateFiles`, `didCreateFiles`, `willRenameFiles`, `didRenameFiles`, `willDeleteFiles`, `didDeleteFiles`

### Document Sync Enhancements

- [ ] Incremental sync (`TextDocumentSyncKind=2`)
- [ ] `textDocument/willSave`: pre-save notification
- [ ] `textDocument/willSaveWaitUntil`: pre-save edits
- [ ] `textDocument/didSave`: post-save notification

### Protocol Infrastructure

- [ ] `$/cancelRequest`: cancel in-flight requests
- [ ] `$/setTrace` + `$/logTrace`: debug tracing
- [ ] `client/registerCapability` + `client/unregisterCapability`: dynamic capability registration
- [ ] Pull-model diagnostics (`textDocument/diagnostic`): client-driven diagnostic refresh
- [ ] `window/showDocument`: open document in client editor
- [ ] `window/showMessageRequest`: messages with response options

### Potential Future Features

- [ ] `textDocument/codeAction` refactoring kinds: `refactor.extract`, `refactor.inline`, `refactor.rewrite`
- [ ] `textDocument/codeAction` source kinds: `source.organizeImports`, `source.fixAll`
- [ ] Incremental document synchronization for large files
- [ ] Multi-file rename across workspace
- [ ] Cross-file go-to-definition (when module system is implemented)
- [ ] Cross-file find-all-references (when module system is implemented)

---

## VS Code Extension

- [ ] Create VS Code extension with `.endo` language registration
- [ ] TextMate grammar for basic syntax highlighting (fallback when LSP is not active)
- [ ] Extension configuration: path to `endo` binary, LSP arguments
- [ ] Snippet support: common patterns (`match`, `let`, `if-then-else`, `for-in`, `fun`)
- [ ] Extension marketplace publishing

---

## Implementation Priority

All Tier 1-4 features are now implemented. Remaining work:

1. **VS Code Extension** — packaging, enables all the above for VS Code users
2. **Incremental document sync** — performance optimization for large files
3. **Workspace features** — as multi-file support matures
4. **Protocol infrastructure** — `$/cancelRequest`, dynamic registration, etc.
