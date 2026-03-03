# Endo LSP Server — Implementation Status & Roadmap

This document tracks the implementation status of Language Server Protocol (LSP 3.17) features
for the Endo language server (`endo --lsp`).

The server reuses the shared `endo-language` library (Lexer, Parser, AST, IDE infrastructure)
and communicates via JSON-RPC 2.0 over stdio.

**Legend:** [x] Implemented | [~] Partial | [ ] Not yet implemented

---

## Tier 1 — Core (High Impact)

Foundation for a useful editing experience. All features here are implemented; partial items note what is still missing.

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
- [ ] `completionItem/resolve`: deferred documentation lookup for expensive items

### Rename

- [x] `textDocument/rename`: scope-aware rename across document
- [x] `textDocument/prepareRename`: identifier range validation before rename

### Document Symbols

- [~] `textDocument/documentSymbol`: hierarchical outline (functions with parameter children, top-level let bindings, record types with field children, union types with variant children, property bindings, finer `SymbolKind` usage)
  - Missing: nested symbols (let-in bindings inside function bodies)

### Formatting

- [~] `textDocument/formatting`: whole document formatting via `SourceFormatter::format()`
  - Missing: `textDocument/rangeFormatting` (format selection only)
  - Missing: `textDocument/onTypeFormatting` (auto-format on keystroke)

### Semantic Tokens

- [~] `textDocument/semanticTokens/full`: 9 token types (`keyword`, `function`, `variable`, `number`, `string`, `operator`, `enumMember`, `comment`, `type`), 2 modifiers (`declaration`, `modification`)
  - Missing: `semanticTokens/full/delta` (incremental updates via `resultId` diffing)
  - Missing: `semanticTokens/range` (partial document tokenization)

---

## Tier 2 — Enhanced Editing

High-value features that significantly improve the day-to-day editing experience.

### Code Actions & Quick Fixes

- [ ] `textDocument/codeAction`: quick fixes derived from diagnostic suggestions
  - "Did you mean?" for misspelled function/variable names
  - Wrap unwrapped Option/Result (diagnostic suggestions already exist in `DiagnosticsCollector`)
  - Add missing `let` keyword, fix common syntax patterns
  - Extract expression to let binding
  - Reuse: `DiagnosticsCollector` already produces `suggestions` vectors per diagnostic
- [ ] `codeAction/resolve`: deferred edit computation for expensive actions

### Document Highlight

- [ ] `textDocument/documentHighlight`: highlight all occurrences of the symbol under cursor
  - Reuse: `SymbolCollector::findReferences()` with `DocumentHighlightKind::Read`/`Write` distinction
  - Low implementation cost — thin wrapper over existing infrastructure

### Folding Ranges

- [ ] `textDocument/foldingRange`: collapsible regions
  - Function bodies (multi-line `let f x = ...`)
  - Match expressions (from `match` to last `|` arm)
  - If-then-else blocks, for/while loop bodies
  - Block scopes `{ ... }`, seq/list comprehension bodies
  - Multi-line comments `(* ... *)`
  - Implementation: AST walk over multi-line nodes

### Selection Range

- [ ] `textDocument/selectionRange`: smart expand/shrink selection
  - Hierarchy: identifier → expression → statement → block → function body → top-level
  - Implementation: AST walk finding all nodes containing cursor position

### Inlay Hints

- [ ] `textDocument/inlayHint`: inline type annotations
  - Inferred parameter types for untyped function parameters (via Hindley-Milner type inference)
  - Inferred return types for function definitions
  - Variable types from `let` bindings
  - Pipeline intermediate types (`data |> map f |> filter g`)
  - Reuse: type inference (Algorithm W) already runs as a pre-pass before IR generation
- [ ] `inlayHint/resolve`: deferred tooltip/location for inlay hints

---

## Tier 3 — Advanced Navigation

Features for navigating larger codebases and understanding code structure.

### Call Hierarchy

- [ ] `textDocument/prepareCallHierarchy`: prepare call hierarchy data
- [ ] `callHierarchy/incomingCalls`: who calls this function
- [ ] `callHierarchy/outgoingCalls`: what does this function call
  - Extend `SymbolCollector` to track call relationships (partially available via `ApplicationExpr` analysis)

### Workspace Symbol

- [ ] `workspace/symbol`: search symbols across all open documents
  - Aggregate `collectSymbols()` results across all documents in `DocumentStore`

### Go to Type Definition

- [ ] `textDocument/typeDefinition`: navigate from variable to its type definition
  - From a variable bound to a record/union → jump to `type` definition
  - Extend `SymbolCollector` to track type definitions and variable-type associations

### Document Link

- [ ] `textDocument/documentLink`: clickable links in source
  - `source "file"` shell commands
  - `File.open "path"` string arguments
  - Future: `import "path"` when module system is implemented
- [ ] `documentLink/resolve`

### Semantic Tokens Incremental

- [ ] `textDocument/semanticTokens/full/delta`: incremental token updates via `resultId` diffing
- [ ] `textDocument/semanticTokens/range`: tokenize only the requested line range

---

## Tier 4 — IDE Polish

Features that make the experience feel premium.

### On-Type Formatting

- [ ] `textDocument/onTypeFormatting`: auto-format on keystroke
  - Trigger on `\n`: auto-indent based on context (function body, match arm, block scope)
  - Trigger on `|`: align match arms

### Range Formatting

- [ ] `textDocument/rangeFormatting`: format selected range only
  - Extract range, format respecting surrounding indentation context

### Code Lens

- [ ] `textDocument/codeLens`: inline annotations above functions
  - "N references" count above function definitions
  - "Run" action for top-level executable scripts
- [ ] `codeLens/resolve`
- [ ] `workspace/codeLens/refresh`

### Inline Value

- [ ] `textDocument/inlineValue`: show variable values during debugging
  - Only meaningful with DAP integration (Phase 5.1 in main roadmap)
- [ ] `workspace/inlineValue/refresh`

### Window Notifications

- [ ] `window/showMessage`: server-to-client informational messages
- [ ] `window/showMessageRequest`: messages with response options
- [ ] `window/logMessage`: structured log output

### Work Done Progress

- [ ] `window/workDoneProgress/create` + `$/progress`: progress reporting for long operations
- [ ] `window/workDoneProgress/cancel`: cancel progress

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
- [ ] File operations: `willCreateFiles`, `didCreateFiles`, `willRenameFiles`, `didRenameFiles`, `willDeleteFiles`, `didDeleteFiles`

### Document Sync Enhancements

- [ ] `textDocument/willSave`: pre-save notification
- [ ] `textDocument/willSaveWaitUntil`: pre-save edits
- [ ] `textDocument/didSave`: post-save notification

### Protocol Infrastructure

- [ ] `$/cancelRequest`: cancel in-flight requests
- [ ] `$/setTrace` + `$/logTrace`: debug tracing
- [ ] `client/registerCapability` + `client/unregisterCapability`: dynamic capability registration
- [ ] Pull-model diagnostics (`textDocument/diagnostic`): client-driven diagnostic refresh

---

## VS Code Extension

- [ ] Create VS Code extension with `.endo` language registration
- [ ] TextMate grammar for basic syntax highlighting (fallback when LSP is not active)
- [ ] Extension configuration: path to `endo` binary, LSP arguments
- [ ] Snippet support: common patterns (`match`, `let`, `if-then-else`, `for-in`, `fun`)
- [ ] Extension marketplace publishing

---

## Implementation Priority

Recommended order of work for maximum impact:

1. **Code Actions** (Tier 2) — highest ROI: diagnostic suggestions already exist, just need LSP wiring
2. **Document Highlight** (Tier 2) — very low cost, reuses `findReferences()`
3. **Folding Ranges** (Tier 2) — moderate cost, immediate usability improvement
4. ~~**Document Symbol completeness** (Tier 1) — add type definitions, nested symbols, union variants~~ (done: types, variants, fields, properties; remaining: nested let-in)
5. **Inlay Hints** (Tier 2) — high value for functional language, requires type inference integration
6. **Selection Range** (Tier 2) — moderate cost, good structural editing support
7. **Semantic Tokens Delta** (Tier 3) — performance optimization for large files
8. **Range Formatting** (Tier 4) — moderate cost
9. **Call Hierarchy** (Tier 3) — extends existing symbol infrastructure
10. **Workspace Symbol** (Tier 3) — low cost aggregation of per-document symbols
11. **VS Code Extension** — packaging, enables all the above for VS Code users
