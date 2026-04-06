---
title: Roadmap
description: Development roadmap and progress for the Endo project.
---

# Roadmap

Endo is a cross-platform shell that combines familiar shell conventions with F#-inspired
functional programming.

## Vision

- **Interactive UX** -- Rich text editing, mouse support, LSP-like completions, and syntax
  highlighting directly in the shell
- **AI-Powered** -- Natural language command generation with provider abstraction (local
  LLMs and cloud APIs)
- **Cross-Platform** -- Linux, macOS, and Windows support
- **Developer-Friendly** -- Debug Adapter Protocol (DAP) integration for script debugging

## Status Overview

| Area | Status | Details |
|------|--------|---------|
| Foundation (Milestone 0) | Complete | UTF-8, platform abstraction, error handling |
| Core Language (Milestone 1) | ~95% complete | Variables, redirects, expansions, control flow, job management, F# extensions |
| Terminal UX (Milestone 2) | Largely complete | Rich editor, mouse, completions, highlighting, prompt customization |
| AI Integration (Milestone 3) | Not started | Provider abstraction, NL commands, intelligent assistance |
| Windows Support (Milestone 4) | Stubs in place | Platform abstraction designed; Windows backends pending |
| Developer Tools (Milestone 5) | Partial | LSP server complete; DAP and profiling pending |
| Structured Data (Milestone 6) | Largely complete | Structured builtins, output recognition, pipeline integration |
| [Language Features](language-features.md) | ~95% complete | F# expressions, pattern matching, records, unions, lists, type inference |
| [Structured Data](structured-data.md) | Largely complete | ps/ls/jobs builtins, output recognition files, table rendering |
| [WebAssembly](wasm.md) | Not started | WASM compilation backend planned |

## Milestone Summary

### Milestone 0: Foundation -- Complete

UTF-8 support, platform abstraction layer (Linux complete, Windows stubs), error handling
with `std::expected`, code deduplication, and CoreVM cleanup.

### Milestone 1: Core Language -- ~95% Complete

Full shell language with variables, redirects, substitutions, expansions, control flow,
job management, and extensive F# syntax extensions including pattern matching, algebraic
types, closures, type inference, records, unions, and lists.

### Milestone 2: Terminal UX -- Largely Complete

Rich text editor with selection, undo/redo, clipboard; mouse integration with passive
tracking; context-aware completions with ghost text; syntax highlighting; hover tooltips;
customizable prompt with 10 presets.

### Milestone 3: AI Integration -- Not Started

Planned features include provider abstraction for multiple LLM backends, natural language
command generation, intelligent command completion, and context-aware assistance.

### Milestone 4: Windows Support -- In Progress

Platform abstraction interfaces are designed with Linux/POSIX backends complete. Windows
stubs (CreateProcess, ConPTY, pipes) await implementation.

### Milestone 5: Developer Tools -- Partial

The LSP server is operational with diagnostics, hover, completion, go-to-definition,
references, rename, semantic tokens, and signature help. DAP server and profiling tools
are planned.

### Milestone 6: Structured Data -- Largely Complete

Built-in structured commands (`ps`, `ls`, `jobs`) return typed records. Output recognition
files enable structured parsing of external commands. Table rendering for record lists.
Pipeline integration with all HOF operations.

## Success Criteria for 1.0

- All Milestone 0, 1, 2, and 4 tasks complete
- Milestone 3 Phase 3.1 and 3.2 complete (basic AI integration)
- Comprehensive test suite passing on Linux and Windows
- Documentation complete (user guide, configuration reference)
- Performance acceptable for interactive use (< 50ms prompt latency)
- No critical or high-severity bugs

## Detailed Roadmaps

- [Language Features](language-features.md) -- F# feature implementation status
- [Structured Data](structured-data.md) -- Structured data and system interaction
- [WebAssembly](wasm.md) -- WASM compilation backend
