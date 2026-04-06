---
title: Shell Overview
description: Endo as an interactive shell — features and usage.
---

# Shell Overview

Endo is a fully interactive shell designed for daily use. It combines familiar shell
conventions with F#-inspired functional programming.

## Key Concepts

### Dual-Mode Prompt

At the same prompt, you can type traditional shell commands or F# expressions. Endo
detects which mode to use based on the syntax:

```endo
# Shell mode -- runs external commands
ls -la
git status && echo "clean"

# F# mode -- triggered by `let`, `match`, list literals, etc.
let files = [1; 2; 3] |> map (_ * 2)
match (env "EDITOR") with
| Some e -> println $"Editor: {e}"
| None   -> println "No editor set"
```

There is no mode switch command. The parser recognizes the context automatically.

### Structured Pipelines

Traditional shells pipe raw text between processes. Endo adds a second pipeline operator
(`|>`) that passes typed values between functions:

<!-- endo-no-check -->
```endo
# Shell pipe: bytes between OS processes
cat access.log | grep 404 | wc -l

# Forward pipe: typed values between functions
[10; 25; 3; 42] |> filter (_ > 10) |> map (_ * 2)
```

You can even transition from shell to functional in a single pipeline:

```endo
ps |> filter (contains _.command "nginx") |> length
```

See the [FAQ](../FAQ.md) for a detailed comparison of `|` vs `|>`.

### Structured Output Recognition

When a command appears before `|>` and a matching output definition exists, Endo
automatically parses the command output into typed records:

```endo
# docker ps output is automatically parsed into records
docker ps |> filter (_.status |> contains "Up") |> map _.names
```

See [Structured Output](structured-output.md) for details.

## Interactive Features

### Syntax Highlighting

Endo highlights your input in real-time as you type. Keywords, strings, numbers,
operators, constructors, and builtins each have distinct colors.

### Context-Aware Completions

Press **Tab** or **Ctrl+Space** to trigger completions. Endo offers:

- **Command completion** -- builtins and executables from `$PATH`
- **File path completion** -- with tilde expansion
- **Variable completion** -- environment variables and F# bindings
- **F# dot-access completion** -- `Option.map`, record fields, method-style calls
- **History-based suggestions** -- fish-style ghost text (dimmed, press Right to accept)

See [Completions](completions.md) for full documentation including writing custom completers.

### Rich Text Editing

The input field supports:

- **Multiline editing** -- Alt+Enter or Shift+Enter for newlines
- **Selection** -- Shift+arrows, Ctrl+A, double-click word, triple-click line
- **Clipboard** -- Ctrl+C copy, Ctrl+V paste, Ctrl+X cut (via OSC 52)
- **Undo/Redo** -- Ctrl+Z / Ctrl+Y
- **Mouse support** -- click to position cursor, drag to select

### Tooltips and Hover

Hover the mouse over a token to see contextual information:

- Command path for external executables
- Documentation for builtins and keywords
- Type information for F# bindings
- Error details for underlined diagnostics

### Customizable Prompt

Endo ships with 10 built-in prompt presets and a modular prompt system:

```endo
set_prompt_preset "endo-signature"
set_prompt_layout "two-line"
```

See [Configuration](configuration.md) for full prompt customization options.

## Key Bindings

| Key | Action |
|-----|--------|
| Enter | Submit command |
| Alt+Enter / Shift+Enter | Insert newline |
| Tab | Trigger completion |
| Ctrl+Space | Trigger completion (always shows popup) |
| Up / Down | History navigation |
| Ctrl+C | Copy selection (or interrupt if no selection) |
| Ctrl+V | Paste |
| Ctrl+X | Cut |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+A / Ctrl+E | Smart cursor to line start / end |
| Ctrl+K / Ctrl+U | Kill to end / start of line |
| Ctrl+W | Delete word backward |
| Ctrl+D | Delete character (or EOF on empty line) |
| Ctrl+L | Clear screen |
| Ctrl+R | History search |
| Right / End / Ctrl+E | Accept ghost text suggestion |

Use the [`bind`](builtins.md#bind) builtin to customize key bindings at runtime.
See [Configuration: Key Bindings](configuration.md#key-bindings) for the full list of default
bindings and available actions.

## Further Reading

- [Configuration](configuration.md) -- Prompt, aliases, key bindings, and environment
- [Built-in Commands](builtins.md) -- Reference for all shell builtins
- [Completions](completions.md) -- Tab completion system and writing custom completers
- [Structured Output](structured-output.md) -- Automatic parsing of command output
- [Platform Differences](platform-differences.md) -- Windows vs POSIX differences
- [FAQ](../FAQ.md) -- Common questions about pipes, lambdas, and patterns
