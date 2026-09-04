---
title: Completions
description: How endo's tab-completion system works, and how to write custom completers.
---

# Completions

Endo ships with a rich, context-aware completion system that activates on **Tab** or
**Ctrl+Space**. Built-in providers handle commands, file paths, variables, F# dot-access,
and history suggestions. On top of that, scripted completers -- written in endo itself --
add command-specific completions for tools like `git`, `ssh`, `cmake`, and more.

## Built-in Completion Types

### Command Completion

When the cursor is at the first token position, endo completes builtins and executables
found on `$PATH`. Results are ranked by a combination of prefix match quality and
history-based recency -- commands you run often appear first.

```
$ gi<Tab>
  git   gist   gimp   ...
```

### Program Name Completion

Some arguments name a *program* rather than a file. `which` is the clearest case: its
arguments are looked up in `$PATH`, so completing them against the current directory would
offer names it cannot resolve. There, endo completes `$PATH` executables instead, annotated
with the path each one resolves to:

```
$ which gi<Tab>
  git       /usr/bin/git
  git-lfs   /usr/bin/git-lfs
  gio       /usr/bin/gio
```

File-path suggestions are suppressed while the prefix looks like a program name. Because
`which` also accepts a path argument, a path-shaped prefix (`./`, `/`, `~`) switches back to
file completion.

The same applies to commands whose arguments name a running process -- `pgrep`, `pidof` and
`pkill` complete against live process names.

### File Path Completion

Anywhere an argument is expected, endo completes file and directory paths. Tilde expansion
(`~`, `~user`) works transparently.

```
$ cat ~/Doc<Tab>
  ~/Documents/
```

Directories are shown with a trailing `/` to distinguish them from files.

#### Automatic quoting

When a completed path contains characters that would otherwise split it into several
tokens — a space, `!`, `$`, a leading `//` (UNC), and so on — endo inserts it **wrapped in
double quotes** so it stays a single argument (or program name):

```
$ cat X:/Program<Tab>
  cat "X:/Program Files/
```

A directory completion leaves the quote open so the next **Tab** continues inside it; a
file (final) completion closes the quote. If you are already inside an open quote when you
press **Tab**, endo escapes the inserted text for that quote instead of opening a new one.
This is the same quoting you would write by hand to run a program at such a path, e.g.
`& "X:/Program Files/tool.exe"` (see
[Command Execution → Quoting the Program Path](../language/command-execution.md#quoting-the-program-path)).

### Variable Completion

After `$` or inside `${`, endo completes both environment variables and special shell
variables (`$?`, `$$`, `$!`).

```
$ echo $HO<Tab>
  $HOME   $HOSTNAME
```

### F# Dot-Access Completion

Inside F# expressions, typing a dot after a module name, a variable, or the underscore
placeholder triggers member completion:

- **Module access** -- `Option.` completes to `map`, `bind`, `defaultValue`, etc.
- **Record field access** -- `_.pid`, `_.user` when the pipeline carries a record type.
- **Value method access** -- `myVar.` shows methods applicable to the bound value's type.

```
[Some 1; None; Some 3] |> filter (Option.▏)
```

### Let Binding Completion

F# `let` bindings and function definitions persisted across REPL prompts are offered as
completions in expression position. This covers both values and user-defined functions.

### History Ghost Text

Endo provides fish-style inline suggestions drawn from command history. As you type, the
best matching previous command appears as dimmed "ghost text" to the right of your cursor.
Press **Right**, **End**, or **Ctrl+E** to accept the suggestion.

## Writing Custom Completers

Custom completers are `.endo` scripts that teach endo how to complete arguments for a
specific external command. They use the `Completion` module to construct structured
completion entries and register a completer function.

### The Completion Module

The `Completion` module provides functions for building and registering completers.

#### `Completion.register`

```endo
Completion.register "command" function_name
```

Registers `function_name` as the completer for `command`. When the user presses Tab after
typing `command`, endo invokes `function_name` with the current arguments and prefix.

!!! note
    The older form `register_completer "command" function_name` is still supported for
    backward compatibility but `Completion.register` is the preferred API.

#### `Completion.entry`

```endo
Completion.entry "text"
```

Creates a plain completion entry with no description. Equivalent to returning a bare
string, but wrapped in the `Completion` type.

#### `Completion.described`

```endo
Completion.described "text" "description"
```

Creates a completion entry with a short description that appears right-aligned in the
completion menu.

#### `Completion.detailed`

```endo
Completion.detailed "text" "description" "markdown detail"
```

Creates a completion entry with both a short description (shown in the menu) and a longer
markdown string displayed in the **detail panel** when the entry is selected.

#### `Completion.text`

```endo
Completion.text entry
```

Extracts the text string from a `CompletionEntry` value. Useful when you need to inspect
or transform entries after construction.

### Completer Function Signature

A completer function receives two arguments:

```
let my_complete args prefix =
    ...
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `args`    | `List<String>` | Tokens after the command name, excluding the word currently being typed. |
| `prefix`  | `String` | The word being typed (may be empty when the user presses Tab after a space). |

The function returns either a `List<String>` (plain completions) or a
`List<CompletionEntry>` (structured completions with descriptions). You can mix both in
the same list -- plain strings are treated as description-less entries.

!!! tip
    The `prefix` is used by endo's fuzzy matcher to filter and rank results. You do not
    need to filter by prefix yourself -- just return all candidates for the current
    argument position and let endo handle the matching.

### Full Example

Here is a completer for a hypothetical `deploy` tool that has subcommands, options, and
dynamic environment names:

```endo
let deploy_complete args prefix =
    let subcommands = [
        Completion.described "push" "Deploy to environment";
        Completion.described "rollback" "Revert last deployment";
        Completion.described "status" "Show deployment status";
        Completion.described "logs" "View deployment logs"
    ]
    let global_options = [
        Completion.described "--env" "Target environment";
        Completion.described "--dry-run" "Preview without deploying";
        Completion.described "--verbose" "Show detailed output";
        Completion.described "--config" "Config file path"
    ]
    let environments =
        $(deploy env list --quiet)
            |> split "\n"
            |> filter (_ != "")
    match args with
    | [] when startsWith "-" prefix -> global_options
    | [] -> subcommands
    | ["push"; "--env"] -> environments
    | ["rollback"; "--env"] -> environments
    | ["push"] when startsWith "-" prefix -> global_options
    | ["rollback"] when startsWith "-" prefix -> global_options
    | _ when startsWith "-" prefix -> global_options
    | _ -> []

Completion.register "deploy" deploy_complete
```

Key patterns used here:

- **`match args with`** dispatches on the tokens already typed, determining what to
  complete next (subcommand, option, or dynamic value).
- **`when startsWith "-" prefix`** guards detect when the user is typing an option flag.
- **`$(command)` substitution** fetches dynamic values at completion time. Results are
  cached for 2 seconds to avoid repeated subprocess calls while the user navigates the
  menu.
- **`Completion.described`** provides short help text for each entry.
- **Returning `[]`** signals that no completions are available, which falls back to file
  path completion.

### Shared Helper Scripts

Completer scripts can call functions defined in other scripts. For example, the bundled
`_ssh_hosts.endo` helper extracts hosts from `~/.ssh/config`:

```endo
let ssh_hosts _ =
    $(grep -i "^Host " ~/.ssh/config)
        |> split "\n"
        |> filter (_ != "")
        |> map (fun line ->
            match split " " line with
            | _ :: host :: _ -> host
            | _ -> "")
        |> filter (_ != "")
        |> filter (fun s -> not (contains "*" s))
```

The `ssh`, `scp`, and `ssh-copy-id` completers all call `ssh_hosts ()` to offer host
completions. Files prefixed with `_` (like `_ssh_hosts.endo`) are loaded first
alphabetically, making their functions available to other completer scripts.

### Where to Put Completers

Completer scripts are loaded from two directories, in this order:

| Location | Purpose |
|----------|---------|
| `~/.config/endo/completers/` | **User completers** -- loaded first, so they can override bundled ones. |
| `share/endo/completers/` (relative to the endo installation) | **Bundled completers** -- shipped with endo. |

If a file with the same name exists in both directories, the user version takes precedence
and the bundled version is skipped. All `.endo` files in each directory are loaded in
alphabetical order.

!!! tip
    To override a bundled completer, create a file with the same name in
    `~/.config/endo/completers/`. To disable a bundled completer entirely, create an empty
    file with the same name.

## The Completion Popup

When completions are available, endo displays a popup menu below the cursor.

### Menu Layout

Each row in the menu shows:

- **Completion text** on the left, with fuzzy-matched characters highlighted.
- **Description** right-aligned in a dimmer color (if the entry has one).

### Detail Panel

When the selected entry has a markdown `detail` string (created via `Completion.detailed`),
a side panel appears next to the menu displaying the rendered markdown content. This is
useful for showing man-page excerpts, usage examples, or extended documentation.

### Keyboard Navigation

| Key | Action |
|-----|--------|
| Tab / Down | Select next item |
| Up | Select previous item |
| Enter | Accept selected completion |
| Escape | Dismiss popup |
| Page Down | Jump down one page |
| Page Up | Jump up one page |

As you continue typing, the menu filters in real-time using fuzzy matching. Items that no
longer match are removed and scores are recalculated.

## Bundled Completers

Endo ships with completers for the following commands:

| Command | Script | Highlights |
|---------|--------|------------|
| `ssh` | `ssh.endo` | Options with descriptions, host completion from `~/.ssh/config` |
| `scp` | `scp.endo` | Options with descriptions, host completion |
| `ssh-copy-id` | `ssh-copy-id.endo` | Options, hosts, file-path option detection |
| `cmake` | `cmake.endo` | Options, `--preset` from project, generators, build configs |
| `ctest` | `ctest.endo` | Options, `--preset` from project, build configs |
| `gh` | `gh.endo` | Full subcommand tree for GitHub CLI |
| `glab` | `glab.endo` | Full subcommand tree for GitLab CLI, with alias resolution |
| `flatpak` | `flatpak.endo` | Subcommands, dynamic app/remote listing |
| `claude` | `claude.endo` | Subcommands, model names, permission modes, output formats |

These serve as good reference implementations when writing your own completers.
