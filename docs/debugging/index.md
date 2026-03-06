---
title: Debugging
description: Debug Endo scripts from your editor using the Debug Adapter Protocol (DAP).
---

# Debugging

Endo includes a built-in debug adapter that speaks the
[Debug Adapter Protocol (DAP)](https://microsoft.github.io/debug-adapter-protocol/),
the same protocol used by VS Code, Neovim, Emacs, and many other editors.
This means you can set breakpoints, step through code, inspect variables, and
evaluate expressions -- all from your favorite editor.

## Quick Start

Launch the debug adapter over stdio:

```bash
endo --dap
```

The adapter reads JSON-RPC messages on stdin and writes responses on stdout.
Any DAP-compatible client can connect to it as an **executable** adapter.

## Capabilities

| Feature | Details |
|---------|---------|
| Source breakpoints | Line, column, conditional, hit-count, log points |
| Function breakpoints | Break when a named function is entered |
| Exception breakpoints | Filters: `runtime-error`, `all` |
| Execution control | Continue, step over, step in, step out, pause |
| Stepping granularity | `line` (default) or `instruction` (bytecode-level) |
| Stack traces | Full call stack with source locations |
| Variable inspection | Locals, globals, structured values (lists, tuples, records, options, results) |
| Expression evaluation | Hover, watch, and REPL contexts |
| Variable mutation | Modify `let mut` bindings at runtime |
| Disassembly | View compiled bytecode instructions |

## AST-Inlined Functions and Stepping

Endo uses two compilation strategies for F# functions:

- **Compiled functions** (all parameters have type annotations) generate their own
  `IRFunction` and are invoked via the `UCALL` instruction. The debugger sees a
  real call boundary, so **Step In** enters the function body, **Step Over** skips
  it, and **Step Out** returns to the caller -- exactly as you would expect.

- **AST-inlined functions** (one or more parameters lack type annotations) are
  expanded at each call site. There is no `UCALL` instruction and the call stack
  depth does not change. When you **Step In**, the debugger walks through the
  inlined body line by line, but no new stack frame appears. **Step Over**
  effectively behaves the same as Step In because there is no call boundary to
  skip.

If you want full step-in/out semantics for a particular function, add type
annotations to all of its parameters. This forces the compiler to generate a
compiled function with a distinct call frame.

## Editor Guides

- [VS Code](vscode.md) -- full setup and usage guide
- [Neovim (nvim-dap)](neovim.md) -- full setup and usage guide
