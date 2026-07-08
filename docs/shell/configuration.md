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

The default `endo-signature` preset uses a transparent prompt background by
default, so the prompt blends with the terminal's own background. Set
`shell_prompt_color_background` to a color (or `"theme"`) to override it.

### User and host

Most presets begin the info line with the `hostname` module, which renders your
identity as `user@host` (fish-style) to the left of the working directory --
for example, `alice@web-prod-01`. It is shown in every session, so when you SSH
into a remote machine the prompt makes the change of host obvious. The username
is taken from `USER` (or `LOGNAME`, or `USERNAME` on Windows) and the hostname
from the local machine. The minimal presets (`minimal-arrow`, `lambda-clean`)
omit it to stay compact. The user and host portions are colored independently
via `shell_prompt_color_username` and `shell_prompt_color_hostname`.

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

The indicator is the character(s) shown just before the input cursor. The string
form is rendered verbatim -- there is no template expansion (for example,
`"${pwd}"` prints the literal text `${pwd}`).

```endo
# Static string form: rendered verbatim, no substitution
shell_prompt_indicator <- "> "
shell_prompt_indicator <- "$ "
```

To produce an indicator that depends on live shell state, assign a
zero-argument function that returns a string. It is invoked at each prompt
render:

```endo
# Dynamic form: named function
let myPrompt () = $"{(env "PWD")?} => "
shell_prompt_indicator <- myPrompt

# Anonymous lambda literal also works
shell_prompt_indicator <- fun () -> $"{(env "PWD")?} => "
```

Notes and limitations:

- Invocation runs synchronously on the main thread; results are cached and
  re-evaluated only when the shell context changes (CWD, exit code, or last-
  command duration). Within a single context, every keystroke reuses the cached
  value so typing is never slowed by the callback.
- For genuinely slow callbacks (network or heavy computation), a background-
  thread execution path analogous to the git status module is still a
  potential future enhancement. Until then, prefer callbacks that are fast
  on the happy path.
- If the function is unknown, throws, or returns an empty string, the static
  `shell_prompt_indicator` value is used as a fallback and the shell stays alive.
- The static and dynamic forms write to separate storage: assigning a string
  clears any dynamic function; assigning a function leaves the static string
  untouched as the fallback.

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

### Completion Timeout and Caching

Some completers run external commands to gather candidates (for example the
`dnf`/`rpm` completers query installed and available packages). Two mechanisms keep
these fast and non-blocking:

- **Ghost text never runs these commands.** The inline autosuggestion shown as you
  type is cache-only for scripted completers — it never spawns a subprocess — so
  typing stays responsive. Only pressing **Tab** may fetch a fresh list.
- **Results are cached, including on disk.** A fetched package list is cached in
  memory for the session and persisted under
  `$XDG_CACHE_HOME/endo/completions/` (falling back to `~/.cache/endo/completions/`),
  so the first Tab of a later session is served instantly from disk. The cache key
  excludes the word being typed, so once a list is cached, further keystrokes filter
  it without re-fetching.

If a fetch is slow or hangs, it is bounded by a timeout so the shell never blocks
indefinitely; pressing **Ctrl+C** also aborts an in-progress completion immediately.
On abort or timeout a short notice is shown and the previously cached list (if any)
is used instead. Aborted and timed-out fetches are never cached, so a cancelled
completion does not suppress the next attempt.

```endo
# Overall upper bound for a completion command (default: 3000 ms)
shell_completion_timeout <- 5000

# Shorter budget for a cold/uncached fetch, so the first Tab stays snappy
# (default: 1000 ms). The effective deadline is the smaller of the two positive
# budgets; 0 disables that budget.
shell_completion_cold_timeout <- 1500

# Disable both timeouts — rely on Ctrl+C only
shell_completion_timeout <- 0
shell_completion_cold_timeout <- 0

# How long (seconds) a cached list is served before it is re-fetched (default: 250)
shell_completion_cache_ttl <- 600

# Disable the persistent on-disk cache (in-memory only). Set in init.endo, which is
# loaded before completers, to take effect for the whole session.
shell_completion_cache_enabled <- false
```

Defaults: overall timeout 3000 ms, cold-fetch timeout 1000 ms, cache freshness
250 s, on-disk cache enabled.

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

### Prompt Color Customization

Endo's prompt colors can be fully customized. Each color property accepts:

- **`"#RRGGBB"`** -- a solid hex color (e.g., `"#FF6600"`)
- **`"#RRGGBB:#RRGGBB:..."`** -- a gradient with colon-separated color stops
- **`"transparent"`** -- use the terminal's default background (background only)
- **`"theme"`** -- reset to the current theme's default color

#### Background Color

The prompt background can be set to a solid color, made transparent (showing the
terminal's default background), or reset to the theme default:

```endo
# Transparent background (terminal default shows through)
shell_prompt_color_background <- "transparent"

# Custom solid background
shell_prompt_color_background <- "#1E1E2E"

# Reset to theme default
shell_prompt_color_background <- "theme"
```

!!! tip
    Transparent backgrounds work best with the `minimal-arrow` preset or single-line
    layouts. Multi-line presets with aurora gradients override the background with their
    own gradient colors regardless of this setting.

#### Solid Colors

Set any prompt element to a solid hex color:

```endo
# Path in bright orange
shell_prompt_color_path <- "#FF6600"

# Green indicator
shell_prompt_color_indicator <- "#50FA7B"

# Red separator bar
shell_prompt_color_separator <- "#FF5555"
```

#### Dynamic Colors

Any color property (except background) also accepts a zero-argument function
that returns a color-spec string. The function is invoked at each context
change (cached between renders within the same context), so the color can
depend on live shell state such as exit code or working directory:

```endo
# Indicator color that changes with the last exit code
let indicatorColor () = "#FF5555"
shell_prompt_color_indicator <- indicatorColor

# Inline lambda form
shell_prompt_color_path <- fun () -> "#5078FF:#00DCC8"
```

If the function returns an unparseable string or fails, the color falls back
to the theme default (same behavior as `"theme"`).

#### Gradient Colors

Any color property (except background) can be set to a multi-stop gradient.
Gradient stops are separated by colons. The gradient is interpolated linearly
across the text of the corresponding prompt module:

```endo
# Blue-to-teal gradient on path (like endo-signature preset)
shell_prompt_color_path <- "#5078FF:#00DCC8"

# Three-stop rainbow gradient on git branch when clean
shell_prompt_color_git_clean <- "#00FF00:#00FFFF:#0000FF"

# Warm gradient for the duration badge
shell_prompt_color_duration <- "#FFB86C:#FF5555"
```

#### Available Color Properties

| Property | Description |
|----------|-------------|
| `shell_prompt_color_background` | Prompt background (supports `transparent`) |
| `shell_prompt_color_path` | Current directory path text |
| `shell_prompt_color_git_clean` | Git branch when repository is clean |
| `shell_prompt_color_git_dirty` | Git branch when unstaged changes exist |
| `shell_prompt_color_git_staged` | Git indicator when staged changes exist |
| `shell_prompt_color_indicator` | Input line indicator (e.g., `|> `) |
| `shell_prompt_color_indicator_error` | Indicator when last command failed |
| `shell_prompt_color_exit_code` | Exit code badge |
| `shell_prompt_color_duration` | Command duration badge |
| `shell_prompt_color_hostname` | Host portion of the `user@host` identity |
| `shell_prompt_color_username` | User portion of the `user@host` identity |
| `shell_prompt_color_separator` | Left bar / separator |
| `shell_prompt_color_badge` | Badge background (F# mode, structured output) |
| `shell_prompt_color_badge_text` | Badge text |
| `shell_prompt_color_clock` | Clock module text |

#### Preset Interaction

Color overrides are applied **on top of** the selected preset. When you switch presets,
all color overrides are reset to the preset's defaults:

```endo
# Start with a preset, then customize individual colors
shell_prompt_preset <- "endo-signature"
shell_prompt_color_background <- "transparent"
shell_prompt_color_path <- "#FF6600:#FFFF00"

# Switching presets resets all overrides
shell_prompt_preset <- "powerline"  # colors reset to powerline defaults
```

#### Example: Custom Theme

```endo
# ~/.config/endo/init.endo

# Start with a clean base
shell_prompt_preset <- "opencode-bar"

# Custom color scheme
shell_prompt_color_background <- "#1A1B26"
shell_prompt_color_path <- "#7AA2F7:#BB9AF7"
shell_prompt_color_git_clean <- "#9ECE6A"
shell_prompt_color_git_dirty <- "#F7768E"
shell_prompt_color_separator <- "#565F89"
shell_prompt_color_indicator <- "#7DCFFF"
shell_prompt_color_indicator_error <- "#F7768E"
```

### Reading Property Values

All prompt properties can be read as expressions:

```endo
# Print current preset
print shell_prompt_preset

# Print current path color
print shell_prompt_color_path

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
| `ctrl+backspace` | Delete word backward (stops at `/`, `.`, `-` boundaries) |
| `alt+backspace` | Delete whole whitespace-delimited word backward |
| `ctrl+w` | Delete word backward (stops at `/`, `.`, `-` boundaries) |
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
| Editing | `delete-char-backward`, `delete-char-forward`, `delete-word`, `delete-word-backward`, `delete-big-word-backward`, `kill-to-end`, `kill-to-start`, `transpose` |
| Undo / Redo | `undo`, `redo` |
| Kill Ring | `yank`, `yank-pop` |
| Selection | `select-all` |
| Clipboard | `cut`, `copy`, `paste` |
| Control | `submit`, `abort`, `insert-newline`, `agent-mode`, `cycle-agent-mode`, `cycle-thinking-mode`, `cycle-model` |
| History | `history-prev`, `history-next` |

!!! note "Under Development"
    Some configuration features (such as vi mode) are still under development.
    See the [Roadmap](../roadmap/index.md) for planned features.
