---
title: Shell Configuration
description: Configure the Endo shell -- startup file, prompt, aliases, key bindings, and environment.
---

# Shell Configuration

Endo can be customized through a startup configuration file, environment variables, prompt
settings, aliases, and key bindings.

## Startup File

Endo loads `~/.config/endo/init.endo` automatically on startup. This file is executed as
regular Endo code, so you can use any shell commands or F# expressions.

```endo
# ~/.config/endo/init.endo

# Prompt configuration
set_prompt_preset "endo-signature"
set_prompt_layout "two-line"

# Aliases
let ll ...args = & exa -l ...args
let gs ...args = & git status ...args
let gd ...args = & git diff ...args

# Environment
export EDITOR=nvim
export PAGER=less
```

!!! tip
    Changes to `init.endo` take effect the next time you start a new Endo session.

## Environment Variables

### Setting Variables

<!-- endo-no-check -->
```endo
# Shell-style export
export PATH="/usr/local/bin:$PATH"
export EDITOR=nvim

# F#-style export binding
let export MY_VAR = "some value"
```

### Reading Variables

```endo
# Shell-style substitution
echo $HOME
echo ${HOME}

# F#-style (returns Option type)
match (env "HOME") with
| Some dir -> println $"Home: {dir}"
| None     -> println "HOME not set"
```

### Special Variables

| Variable | Description |
|----------|-------------|
| `$?` | Exit code of the last command |
| `$$` | PID of the current shell |
| `$!` | PID of the last background process |
| `$0` | Name of the shell or script |
| `$1` - `$9` | Positional parameters |

## Prompt Customization

Endo provides a modular prompt system with built-in presets and fine-grained controls.

### Presets

```endo
set_prompt_preset "minimal-arrow"
set_prompt_preset "lambda-clean"
set_prompt_preset "powerline"
set_prompt_preset "endo-signature"
set_prompt_preset "dashboard"
```

Available presets: `minimal-arrow`, `lambda-clean`, `opencode-bar`, `powerline`,
`transient`, `dashboard`, `boxed-module`, `gradient-glow`, `context-adaptive`,
`endo-signature`.

### Layout

```endo
# Single line prompt
set_prompt_layout "single-line"

# Two-line prompt (info on top, input below)
set_prompt_layout "two-line"

# Boxed prompt with borders
set_prompt_layout "boxed"
```

### Prompt Indicator

```endo
# Change the input indicator character
set_prompt_indicator "> "
set_prompt_indicator "$ "
```

### Separator Style

```endo
set_prompt_separator "powerline"
set_prompt_separator "arrow"
set_prompt_separator "rounded"
set_prompt_separator "none"
```

### Transient Prompt

When enabled, the full prompt is replaced with a compact indicator after a command is
submitted, keeping the scrollback clean:

```endo
set_prompt_transient "enabled"
set_prompt_transient "disabled"
```

### Prompt Spacing

Control the number of blank lines above and below the prompt:

```endo
# Add a blank line above and below the prompt (default)
set_prompt_spacing 1

# No blank lines around the prompt
set_prompt_spacing 0
```

### Command Duration Threshold

Control when the command duration module appears:

```endo
# Show duration for commands taking longer than 2 seconds
set_prompt_duration_threshold 2000
```

## Aliases

Endo uses F# function definitions with variadic parameters as aliases:

```endo
# Define aliases with splat syntax
let ll ...args = & exa --long --group --header ...args
let la ...args = & exa -la ...args
let gs ...args = & git status ...args
let gco ...args = & git checkout ...args
```

These aliases support argument passthrough -- any additional arguments you provide are
forwarded to the underlying command.

```endo
# Usage:
ll                      # runs: exa --long --group --header
ll /tmp                 # runs: exa --long --group --header /tmp
gs -s                   # runs: git status -s
```

## Key Bindings

### The `bind` Builtin

Use the `bind` command to view and modify key bindings at runtime:

```endo
# List all current bindings
bind

# Bind a key to an action
bind ctrl+y yank

# Remove a binding
bind -r ctrl+y

# Reset to defaults
bind --reset

# Show available actions and key format
bind --help
```

### Key Format

Keys are specified as modifier+key combinations:

- `ctrl+a`, `ctrl+shift+a`
- `alt+f`, `alt+b`
- `shift+left`, `shift+right`
- `home`, `end`, `delete`
- `f1` through `f12`

### Default Bindings

| Key | Action |
|-----|--------|
| `ctrl+a` | Select all |
| `ctrl+c` | Copy (or interrupt) |
| `ctrl+d` | Delete character / EOF |
| `ctrl+e` | Move to end of line |
| `ctrl+k` | Kill to end of line |
| `ctrl+u` | Kill to start of line |
| `ctrl+w` | Kill word backward |
| `ctrl+y` | Redo |
| `ctrl+z` | Undo |
| `ctrl+l` | Clear screen |

!!! note "Under Development"
    Some configuration features (such as vi mode and configurable color schemes) are still
    under development. See the [Roadmap](../roadmap/index.md) for planned features.
