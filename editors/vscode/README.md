# Endo for Visual Studio Code

Full language support and integrated debugging for the [Endo](https://github.com/contour-terminal/endo) shell language.

## Features

- **Syntax highlighting** -- TextMate grammar covering keywords, strings (shell-style, literal, and F#-style interpolated), numbers, size/timespan literals, comments, operators, and types
- **Language intelligence** (via LSP) -- hover info, completions, go-to-definition, references, rename, document symbols, signature help, formatting, inlay hints, semantic tokens
- **Integrated debugging** (via DAP) -- breakpoints (source, conditional, hit-count, log, function), stepping (over/into/out, instruction-level), variable inspection, watch expressions, debug console REPL, exception breakpoints, variable modification, disassembly
- **Code snippets** -- common patterns like `let`, `fun`, `match`, `if`, `for`, `type`, `seq`, `try`
- **Zero-config F5** -- press F5 on any `.endo` file to debug it immediately, no `launch.json` required

## Requirements

- The `endo` binary must be available on your `PATH`, or configure `endo.path` in settings to point to it.

## Quick Start

1. Install the extension
2. Open any `.endo` file
3. Press **F5** to start debugging

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `endo.path` | `"endo"` | Path to the endo binary |
| `endo.lsp.enable` | `true` | Enable the language server |
| `endo.lsp.trace` | `"off"` | Trace LSP communication (`"off"`, `"messages"`, `"verbose"`) |

## launch.json Configuration

```json
{
  "type": "endo",
  "request": "launch",
  "name": "Launch Endo Script",
  "program": "${file}",
  "args": [],
  "stopOnEntry": false,
  "noDebug": false
}
```

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `program` | string | *required* | Path to the `.endo` script |
| `args` | string[] | `[]` | Arguments passed to the script |
| `stopOnEntry` | boolean | `false` | Pause at the first line |
| `noDebug` | boolean | `false` | Run without debugging |

## Documentation

See the [full documentation](https://contour-terminal.github.io/endo/) for the Endo language and debugging guides.
