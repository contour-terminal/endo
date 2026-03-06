---
title: VS Code
description: Set up Endo script debugging in Visual Studio Code.
---

# Debugging Endo Scripts in VS Code

This guide walks you through using the Endo VS Code extension
to debug Endo scripts with full IDE integration.

## Prerequisites

- Visual Studio Code 1.85 or later
- The Endo VS Code extension installed
- `endo` binary on your `$PATH` (or configured via `endo.path` setting)

## Installation

### From .vsix File

```bash
cd editors/vscode
npm install && npm run compile && npm run package
code --install-extension endo-0.1.0.vsix
```

### From Source (Development)

1. Open the `editors/vscode` folder in VS Code
2. Press **F5** to launch the Extension Development Host
3. Open an `.endo` file in the new window

## Quick Start

1. Open any `.endo` file in VS Code.
2. Click in the gutter to set a breakpoint (or press `F9`).
3. Press **F5** to start debugging.

That's it -- no `launch.json` needed. The extension automatically launches the
current file with the Endo debugger.

## launch.json Configuration

For more control, create a `.vscode/launch.json` in your workspace:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "endo",
      "request": "launch",
      "name": "Launch Endo Script",
      "program": "${file}"
    },
    {
      "type": "endo",
      "request": "launch",
      "name": "Launch with Arguments",
      "program": "${file}",
      "args": ["arg1", "arg2"]
    },
    {
      "type": "endo",
      "request": "launch",
      "name": "Launch (Stop on Entry)",
      "program": "${file}",
      "stopOnEntry": true
    }
  ]
}
```

### Launch Arguments Reference

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `program` | string | *required* | Path to the `.endo` script to debug |
| `args` | string[] | `[]` | Command-line arguments passed to the script |
| `stopOnEntry` | boolean | `false` | Pause at the first line before any code runs |
| `noDebug` | boolean | `false` | Run without debug instrumentation (no breakpoints or stepping) |

## Features

### Breakpoints

**Source breakpoints** -- click in the gutter or press `F9` to toggle a
breakpoint on the current line.

**Conditional breakpoints** -- right-click in the gutter and choose
"Add Conditional Breakpoint..." to pause only when an expression is true.

**Hit-count breakpoints** -- right-click in the gutter and choose
"Add Conditional Breakpoint..." then select "Hit Count" to pause after
a certain number of hits.

**Log points** -- right-click in the gutter and choose "Add Logpoint..."
to print a message without stopping. Expressions inside `{...}` are
interpolated.

**Function breakpoints** -- in the Breakpoints pane, click the `+` button
and enter a function name to pause when that function is entered.

### Stepping

Once paused, use the debug toolbar or keyboard shortcuts:

| Action | Shortcut | Description |
|--------|----------|-------------|
| Continue | `F5` | Resume until next breakpoint or program end |
| Step Over | `F10` | Execute current line, skip over function calls |
| Step Into | `F11` | Step into function calls |
| Step Out | `Shift+F11` | Run until current function returns |
| Pause | `F6` | Interrupt a running program |

Endo also supports **instruction-level stepping** (one bytecode operation at
a time). Toggle instruction stepping via the debug toolbar's stepping
granularity option.

### Inspecting Variables

When paused, the **Variables** pane in the Run and Debug sidebar shows:

- **Locals** -- variables in the current function
- **Globals** -- shell environment and global bindings

Structured values (lists, tuples, records, options, results) can be expanded
to inspect their children.

### Watch Expressions

Add expressions to the **Watch** pane to see their values update on each stop.

### Debug Console (REPL)

The Debug Console lets you evaluate arbitrary Endo expressions in the current
scope. Type an expression and press Enter.

### Exception Breakpoints

Endo provides two exception breakpoint filters, available in the
**Breakpoints** pane:

| Filter | Description |
|--------|-------------|
| Runtime Errors | Break when a runtime error occurs |
| All Errors | Break on any error |

### Modifying Variables

For mutable bindings (`let mut`), double-click a variable in the Variables
pane to edit its value. Immutable bindings will be rejected by the debugger.

### Disassembly

Endo supports the DAP disassembly request. Right-click in the editor while
debugging and choose "Open Disassembly View" to see compiled bytecode
instructions with source locations.

## Settings Reference

| Setting | Default | Description |
|---------|---------|-------------|
| `endo.path` | `"endo"` | Path to the endo binary. Set this if `endo` is not on your PATH. |
| `endo.lsp.enable` | `true` | Enable the Endo Language Server for code intelligence (hover, completions, diagnostics, etc.). |
| `endo.lsp.trace` | `"off"` | Trace LSP communication for debugging. Values: `"off"`, `"messages"`, `"verbose"`. |

## Troubleshooting

### "Cannot find endo binary"

Make sure `endo` is on your `PATH` or set `endo.path` in VS Code settings
(`Ctrl+,` then search for "endo.path").

### Breakpoints are not hit

- Verify the breakpoint is on a line that contains executable code (not a
  comment or blank line). The debugger resolves breakpoints to the nearest
  valid instruction.
- Check the breakpoint verification status -- unverified breakpoints show
  a grey circle with a reason on hover.

### Language features not working

- Check that `endo.lsp.enable` is `true` (default).
- Open the Output panel (`Ctrl+Shift+U`) and select "Endo Language Server"
  to see server logs.
- Set `endo.lsp.trace` to `"verbose"` for detailed protocol traces.

### Debug adapter crashes or no response

Run `endo --dap` manually in a terminal and send a raw initialize request to
verify the adapter starts correctly:

```bash
echo 'Content-Length: 91\r\n\r\n{"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"test"}}' | endo --dap
```

You should see a JSON response with the adapter's capabilities.
