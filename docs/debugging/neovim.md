---
title: Neovim (nvim-dap)
description: Set up Endo script debugging in Neovim using the nvim-dap plugin.
---

# Debugging Endo Scripts in Neovim

This guide walks you through setting up [nvim-dap](https://github.com/mfussenegger/nvim-dap)
to debug Endo scripts directly from Neovim.

## Prerequisites

- Neovim 0.8 or later
- [nvim-dap](https://github.com/mfussenegger/nvim-dap) plugin installed
- `endo` binary on your `$PATH` (or an absolute path in the config below)

Optional but recommended:

- [nvim-dap-ui](https://github.com/rcarriga/nvim-dap-ui) for a visual debugger UI
  (variables, stack, breakpoints, REPL panes)
- [nvim-dap-virtual-text](https://github.com/theHamsta/nvim-dap-virtual-text) for
  inline variable values

## Quick Setup

A ready-to-use configuration file is available at
[`editors/neovim/dap.lua`](../../editors/neovim/dap.lua). Copy it to
`~/.config/nvim/after/plugin/dap-endo.lua` or source it from your init file.

## Configuration

Add the following to your Neovim configuration (e.g. `~/.config/nvim/lua/dap-endo.lua`
or wherever you configure nvim-dap):

### Adapter

```lua
local dap = require("dap")

dap.adapters.endo = {
  type = "executable",
  command = "endo",
  args = { "--dap" },
}
```

### Launch Configuration

```lua
dap.configurations.endo = {
  {
    type = "endo",
    request = "launch",
    name = "Launch Endo Script",
    program = "${file}",
    stopOnEntry = false,
  },
  {
    type = "endo",
    request = "launch",
    name = "Launch with Arguments",
    program = "${file}",
    args = function()
      local input = vim.fn.input("Script arguments: ")
      return vim.split(input, " ", { trimempty = true })
    end,
    stopOnEntry = false,
  },
  {
    type = "endo",
    request = "launch",
    name = "Launch (Stop on Entry)",
    program = "${file}",
    stopOnEntry = true,
  },
}
```

### Launch Arguments Reference

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `program` | string | *required* | Path to the `.endo` script to debug |
| `args` | string[] | `[]` | Command-line arguments passed to the script |
| `stopOnEntry` | boolean | `false` | Pause at the first line before any code runs |
| `noDebug` | boolean | `false` | Run without debug instrumentation (no breakpoints or stepping) |

### Filetype Detection

Register `.endo` files so that nvim-dap can match configurations by filetype:

```lua
vim.filetype.add({
  extension = {
    endo = "endo",
  },
})
```

## Usage

### Starting a Debug Session

1. Open an `.endo` script in Neovim.
2. Set breakpoints with `:lua require("dap").toggle_breakpoint()` (or bind it
   to a key -- see [Key Bindings](#suggested-key-bindings) below).
3. Start debugging with `:lua require("dap").continue()`.

The debugger launches your script and pauses at the first breakpoint hit.

### Breakpoints

**Source breakpoints** are set on specific lines:

```lua
-- Toggle a breakpoint on the current line
require("dap").toggle_breakpoint()
```

**Conditional breakpoints** only pause when an expression evaluates to true:

```lua
require("dap").set_breakpoint(vim.fn.input("Condition: "))
```

**Hit-count breakpoints** pause after a certain number of hits:

```lua
require("dap").set_breakpoint(nil, vim.fn.input("Hit count: "))
```

**Log points** print a message without stopping execution. Expressions inside
`{...}` are interpolated:

```lua
require("dap").set_breakpoint(nil, nil, vim.fn.input("Log message: "))
```

**Function breakpoints** pause when a named function is entered. Use the
nvim-dap REPL or UI to add these.

### Stepping

Once paused, control execution with these commands:

| Action | Command | Description |
|--------|---------|-------------|
| Continue | `dap.continue()` | Resume until next breakpoint or program end |
| Step Over | `dap.step_over()` | Execute current line, skip over function calls |
| Step Into | `dap.step_into()` | Step into function calls |
| Step Out | `dap.step_out()` | Run until current function returns |
| Pause | `dap.pause()` | Interrupt a running program |

Endo also supports **instruction-level stepping** (one bytecode operation at a
time). Pass `{ granularity = "instruction" }` to the step functions if your
nvim-dap version supports it.

### Inspecting Variables

When paused, use the nvim-dap-ui **Scopes** pane or the REPL to inspect:

- **Locals** -- variables in the current function
- **Globals** -- shell environment and global bindings

Structured values (lists, tuples, records, options, results) can be expanded
to inspect their children.

### Evaluating Expressions

The nvim-dap REPL (`:lua require("dap").repl.open()`) lets you evaluate
expressions in three contexts:

- **Hover** -- hover over a variable name to see its value
- **Watch** -- add watch expressions that update on each stop
- **REPL** -- type arbitrary Endo expressions to execute in the current scope

### Exception Breakpoints

Endo provides two exception breakpoint filters:

| Filter ID | Label | Description |
|-----------|-------|-------------|
| `runtime-error` | Runtime Errors | Break when a runtime error occurs |
| `all` | All Errors | Break on any error |

Set exception breakpoints via the nvim-dap REPL:

```
:lua require("dap").set_exception_breakpoints({"runtime-error"})
```

Or break on all errors:

```
:lua require("dap").set_exception_breakpoints({"all"})
```

### Modifying Variables

For mutable bindings (`let mut`), you can change a variable's value while
paused. Use the nvim-dap-ui **Variables** pane or the REPL `set` command.
Immutable bindings will be rejected by the debugger.

### Disassembly

Endo supports the DAP disassembly request, allowing you to view compiled
bytecode instructions with source locations. This is available through
DAP clients that support the `disassemble` capability.

## Suggested Key Bindings

```lua
local dap = require("dap")

vim.keymap.set("n", "<F5>", dap.continue, { desc = "Debug: Continue" })
vim.keymap.set("n", "<F10>", dap.step_over, { desc = "Debug: Step Over" })
vim.keymap.set("n", "<F11>", dap.step_into, { desc = "Debug: Step Into" })
vim.keymap.set("n", "<F12>", dap.step_out, { desc = "Debug: Step Out" })
vim.keymap.set("n", "<leader>b", dap.toggle_breakpoint, { desc = "Debug: Toggle Breakpoint" })
vim.keymap.set("n", "<leader>B", function()
  dap.set_breakpoint(vim.fn.input("Condition: "))
end, { desc = "Debug: Conditional Breakpoint" })
vim.keymap.set("n", "<leader>dr", dap.repl.open, { desc = "Debug: Open REPL" })
vim.keymap.set("n", "<leader>dl", dap.run_last, { desc = "Debug: Run Last" })
```

## Complete Example

Here is a minimal, self-contained configuration that you can drop into
`~/.config/nvim/after/plugin/dap-endo.lua`:

```lua
local dap = require("dap")

-- Adapter: tell nvim-dap how to launch the Endo debug adapter
dap.adapters.endo = {
  type = "executable",
  command = "endo",
  args = { "--dap" },
}

-- Configurations: how to launch Endo scripts
dap.configurations.endo = {
  {
    type = "endo",
    request = "launch",
    name = "Launch Current File",
    program = "${file}",
    stopOnEntry = false,
  },
}

-- Filetype detection
vim.filetype.add({ extension = { endo = "endo" } })

-- Key bindings
vim.keymap.set("n", "<F5>", dap.continue, { desc = "Debug: Continue" })
vim.keymap.set("n", "<F10>", dap.step_over, { desc = "Debug: Step Over" })
vim.keymap.set("n", "<F11>", dap.step_into, { desc = "Debug: Step Into" })
vim.keymap.set("n", "<F12>", dap.step_out, { desc = "Debug: Step Out" })
vim.keymap.set("n", "<leader>b", dap.toggle_breakpoint, { desc = "Debug: Toggle Breakpoint" })
```

## Troubleshooting

### "Adapter not found" or "No configuration found"

Make sure the filetype is set to `endo`. Check with `:set ft?`. If it shows
something else, add the filetype detection snippet from above.

### Breakpoints are not hit

- Verify the breakpoint is on a line that contains executable code (not a
  comment or blank line). The debugger resolves breakpoints to the nearest
  valid instruction.
- Check the breakpoint verification status in nvim-dap-ui -- unverified
  breakpoints include a reason message.

### "Could not find program"

The `program` field must be an absolute path or use `${file}` (which nvim-dap
expands to the current buffer's absolute path). Relative paths are resolved
from the working directory where Neovim was started.

### Debug adapter crashes or no response

Run `endo --dap` manually in a terminal and send a raw initialize request to
verify the adapter starts correctly:

```bash
echo 'Content-Length: 91\r\n\r\n{"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"test"}}' | endo --dap
```

You should see a JSON response with the adapter's capabilities.

### Viewing DAP protocol messages

Enable nvim-dap logging to see all messages exchanged with the adapter:

```lua
require("dap").set_log_level("TRACE")
```

Logs are written to `~/.cache/nvim/dap.log` (or `:lua print(vim.fn.stdpath("cache") .. "/dap.log")`).
