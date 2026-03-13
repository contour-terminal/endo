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
shell_prompt_preset <- "endo-signature"
shell_prompt_layout <- "two-line"

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

To skip loading the startup file entirely, pass `--no-profile` on the command line:

```bash
endo --no-profile
endo --no-profile -c 'echo "no init.endo loaded"'
```

This is useful for debugging, benchmarking, or when a broken `init.endo` prevents the
shell from starting.

## Environment Variables

### Setting Variables

<!-- endo-no-check -->
```endo
# F#-style export binding
let export PATH = $"/usr/local/bin:{(env 'PATH')?}"
let export EDITOR = "nvim"
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
Prompt settings are **builtin properties** -- assign with `<-` and read as expressions.

### Presets

```endo
shell_prompt_preset <- "minimal-arrow"
shell_prompt_preset <- "lambda-clean"
shell_prompt_preset <- "powerline"
shell_prompt_preset <- "endo-signature"
shell_prompt_preset <- "dashboard"
```

Available presets: `minimal-arrow`, `lambda-clean`, `opencode-bar`, `powerline`,
`transient`, `dashboard`, `boxed-module`, `gradient-glow`, `context-adaptive`,
`endo-signature`.

### Layout

```endo
# Single line prompt
shell_prompt_layout <- "single-line"

# Two-line prompt (info on top, input below)
shell_prompt_layout <- "two-line"

# Boxed prompt with borders
shell_prompt_layout <- "boxed"
```

### Prompt Indicator

```endo
# Change the input indicator character
shell_prompt_indicator <- "> "
shell_prompt_indicator <- "$ "
```

### Separator Style

```endo
shell_prompt_separator <- "powerline"
shell_prompt_separator <- "arrow"
shell_prompt_separator <- "rounded"
shell_prompt_separator <- "none"
```

### Transient Prompt

When enabled, the full prompt is replaced with a compact indicator after a command is
submitted, keeping the scrollback clean:

```endo
shell_prompt_transient <- "enabled"
shell_prompt_transient <- "disabled"
```

### Prompt Spacing

Control the number of blank lines above and below the prompt:

```endo
# Add a blank line above and below the prompt (default)
shell_prompt_spacing <- 1

# No blank lines around the prompt
shell_prompt_spacing <- 0
```

### Command Duration Threshold

Control when the command duration module appears:

```endo
# Show duration for commands taking longer than 2 seconds
shell_prompt_duration_threshold <- 2000
```

### Exit Confirmation

Control the double Ctrl+D exit confirmation behavior. The first Ctrl+D press shows
a hint; only a second press within the timeout exits the shell:

```endo
# Set a 2-second confirmation window
shell_exit_confirm_timeout <- 2000

# Disable confirmation (immediate exit on first Ctrl+D)
shell_exit_confirm_timeout <- 0
```

The default timeout is 1000 ms.

### File Listing Icons

When listing files with `ls`, Nerd Font icons are shown next to each filename by default.
You can disable icons while keeping colors:

```endo
# Disable file icons in ls output
shell_ls_icons <- false

# Re-enable file icons
shell_ls_icons <- true
```

This requires a [Nerd Font](https://www.nerdfonts.com/) to render icons correctly.

### Directory Trailing Slash

By default, directory names in `ls` output are shown with a trailing `/` suffix for easy identification:

```endo
# Disable trailing slash on directories
shell_ls_directory_slash <- false

# Re-enable trailing slash (default)
shell_ls_directory_slash <- true
```

### Interactive Mode Detection

The read-only property `shell_is_interactive` indicates whether the shell is running
interactively (REPL) or non-interactively (script or `-c` command):

```endo
# In init.endo — only apply interactive settings when appropriate
if shell_is_interactive then
    shell_prompt_preset <- "endo-signature"
    shell_prompt_layout <- "two-line"
```

This is useful in `init.endo` to conditionally apply settings that only make sense in
interactive mode (prompt customization, key bindings, etc.). The property is read-only;
attempting to assign to it produces a compile-time error.

### Reading Property Values

All prompt properties can be read as expressions:

```endo
# Print current preset
print shell_prompt_preset

# Use in conditionals
if shell_prompt_spacing == 0 then println "Compact mode"
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

#### Undo / Redo

| Key | Action |
|-----|--------|
| `ctrl+z` | Undo |
| `ctrl+y` | Redo |
| `ctrl+shift+z` | Redo |

#### Clipboard & Selection

| Key | Action |
|-----|--------|
| `ctrl+c` | Copy (or interrupt if no selection) |
| `ctrl+x` | Cut |
| `ctrl+shift+a` | Select all |

!!! tip
    Paste has no default binding. Terminal paste is handled natively via the terminal's
    paste event (`ctrl+v` or `ctrl+shift+v` depending on terminal). To bind the kill ring
    paste action, run: `bind ctrl+v paste`

#### Movement

| Key | Action |
|-----|--------|
| `ctrl+a` | Smart move to line start (toggles between first non-space and column 0) |
| `ctrl+e` | Smart move to line end |
| `ctrl+f` | Move forward one character |
| `ctrl+b` | Move backward one character |
| `alt+f` | Move forward one word |
| `alt+b` | Move backward one word |
| `ctrl+p` | Move up one line |
| `ctrl+n` | Move down one line |
| `left` | Move backward one character |
| `right` | Move forward one character |
| `ctrl+left` | Move backward one word |
| `ctrl+right` | Move forward one word |
| `up` | Move up / history previous |
| `down` | Move down / history next |
| `home` | Move to line start |
| `end` | Move to line end |
| `ctrl+home` | Move to buffer start |
| `ctrl+end` | Move to buffer end |

#### Editing

| Key | Action |
|-----|--------|
| `backspace` | Delete character backward |
| `delete` | Delete character forward |
| `ctrl+d` | Delete character forward (EOF on empty line) |
| `ctrl+backspace` | Delete word backward |
| `alt+backspace` | Delete word backward |
| `ctrl+w` | Delete word backward |
| `alt+d` | Delete word forward |
| `ctrl+k` | Kill to end of line |
| `ctrl+u` | Kill to start of line |

!!! tip
    Transpose has no default binding. To bind it, run: `bind ctrl+t transpose`

#### Agent Mode

| Key | Action |
|-----|--------|
| `ctrl+t` | Enter agent mode |
| `shift+tab` | Cycle agent sub-modes |
| `ctrl+/` | Cycle thinking modes |
| `ctrl+.` | Cycle model |

#### Kill Ring

| Key | Action |
|-----|--------|
| `alt+y` | Yank pop (cycle through kill ring) |

!!! tip
    Yank has no default binding (it was `ctrl+y` which is now Redo). To restore the
    Emacs-style binding, run: `bind ctrl+y yank`

#### Control

| Key | Action |
|-----|--------|
| `enter` | Submit command |
| `shift+enter` | Insert newline (multiline editing) |
| `alt+enter` | Insert newline (multiline editing) |
| `tab` | Trigger completion |
| `ctrl+space` | Trigger completion (always shows popup) |
| `ctrl+l` | Clear screen |
| `ctrl+r` | History search |
| `right` / `end` / `ctrl+e` | Accept ghost text suggestion (when at end of line) |

### Action Reference

All bindable action names for use with `bind`:

| Category | Actions |
|----------|---------|
| Movement | `move-forward-char`, `move-backward-char`, `move-forward-word`, `move-backward-word`, `move-to-line-start`, `move-to-line-end`, `move-to-buffer-start`, `move-to-buffer-end`, `move-up`, `move-down`, `smart-move-to-line-start`, `smart-move-to-line-end` |
| Editing | `delete-char-backward`, `delete-char-forward`, `delete-word`, `delete-word-backward`, `kill-to-end`, `kill-to-start`, `transpose` |
| Undo / Redo | `undo`, `redo` |
| Kill Ring | `yank`, `yank-pop` |
| Selection | `select-all` |
| Clipboard | `cut`, `copy`, `paste` |
| Control | `submit`, `abort`, `insert-newline`, `agent-mode`, `cycle-agent-mode`, `cycle-thinking-mode`, `cycle-model` |
| History | `history-prev`, `history-next` |

!!! note "Under Development"
    Some configuration features (such as vi mode and configurable color schemes) are still
    under development. See the [Roadmap](../roadmap/index.md) for planned features.
