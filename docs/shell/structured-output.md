---
title: Structured Output Recognition
description: How Endo automatically parses command output into typed records.
---

# Structured Output Recognition

Endo can automatically parse the output of external commands into typed records, enabling
functional pipeline processing without manual text parsing. This is powered by **Output
Recognition Files** -- declarative YAML definitions that teach Endo how to interpret
command output.

## The Problem

The number of command-line tools is vast. While Endo provides structured built-in commands
(`ls`, `ps`, `jobs`), the shell's utility is greatly increased when it can understand the
output of any command -- `docker`, `git`, `kubectl`, and more -- without requiring a new
builtin for each.

## How It Works

When a command appears before `|>` in a pipeline, Endo checks for a matching output
definition. If found, it:

1. Selects the best-matching variant based on command arguments
2. Optionally runs an alternative command (e.g., adding `--format json`)
3. Parses the output into a stream of typed records
4. Passes the records into the F# pipeline

```endo
# Endo intercepts `docker ps`, runs `docker ps --format json`,
# and parses the JSON output into records automatically.
docker ps |> filter (_.status |> contains "Up") |> map _.names
```

## Output Definition Files

### File Format

Definition files use YAML and follow the naming convention `command.endo-output.yml`.

```yaml
# The command this definition applies to.
command: "command-name"

# One or more parsing variants.
variants:
  - name: "variant-name"

    # Which arguments trigger this variant.
    matches:
      - ["subcommand"]
      - ["subcommand", "-a"]

    # Higher priority wins when multiple variants match.
    priority: 10

    # (Optional) Run this command instead of the user's command.
    # {args} is replaced with any additional user arguments.
    command_to_run: "command-name --format json {args}"

    # Parser configuration.
    parser:
      type: "json"       # or "fields", "table", "regex"
      format: "lines"    # parser-specific options
```

### Search Paths

Endo searches for definition files in these locations (in order):

1. **User-specific:** `~/.config/endo/definitions/`
2. **System-wide:** `/usr/share/endo/definitions/`
3. **Bundled defaults:** shipped with Endo for common commands

User definitions take priority over system and bundled definitions.

## Parser Types

### JSON Parser

For commands that can output JSON. This is the most reliable parser and should be
preferred whenever available.

**Options:**

| Option | Type | Description |
|--------|------|-------------|
| `format` | string | `"lines"` for NDJSON (one object per line) or `"array"` for a JSON array. Default: `"lines"`. |
| `json_path` | string | Optional JSONPath expression to extract data from nested structures. |

**Example:**

```yaml
parser:
  type: "json"
  format: "lines"
```

### Fields Parser

For delimited output formats (NUL-separated, space-separated, etc.).

**Options:**

| Option | Type | Description |
|--------|------|-------------|
| `delimiter` | string | Field separator. Use `\0` for NUL, `whitespace` for any whitespace. |
| `max_fields` | int | Maximum number of fields to split into (remaining text goes into the last field). |
| `columns` | list | Column definitions with `name` and `type`. |

**Example:**

```yaml
parser:
  type: "fields"
  delimiter: "\0"
  columns:
    - { name: "path", type: "string" }
    - { name: "size", type: "int" }
```

### Table Parser

For column-oriented tabular output with headers.

**Options:**

| Option | Type | Description |
|--------|------|-------------|
| `header` | bool/int | `true` to use first line as header; integer N to skip N lines first. |
| `delimiter` | string | Column separator. `"whitespace"` treats any whitespace run as one delimiter. |
| `columns` | list | Column definitions with `name`, `type`, and optional `remaining: true` flag for the last column. |
| `types` | map | Map of `ColumnName: Type` for type hints. |

### Regex Parser

For complex output formats that are not simple tables. Use as a last resort.

**Options:**

| Option | Type | Description |
|--------|------|-------------|
| `line_regex` | string | Regex to filter which lines to process. |
| `format_regex` | string | Regex with capture groups (one per column). |
| `columns` | list | Column definitions matching each capture group. |

## Examples

### Docker Containers

**`docker.endo-output.yml`** (bundled with Endo):

```yaml
command: "docker"
variants:
  - name: "ps-json"
    matches:
      - ["ps"]
      - ["ps", "-a"]
    priority: 10
    command_to_run: "docker ps --format json {args}"
    parser:
      type: "json"
      format: "lines"
```

**Usage:**

```endo
# List running container names
docker ps |> map _.names

# Filter by status
docker ps -a |> filter (_.status |> contains "Exited") |> map _.names

# Count running containers
docker ps |> length
```

### Git Log

**`git.endo-output.yml`** (bundled with Endo):

```yaml
command: "git"
variants:
  - name: "log-json"
    matches:
      - ["log"]
    priority: 10
    command_to_run: 'git log --format={"sha":"%H","author":"%an","message":"%s"} {args}'
    parser:
      type: "json"
      format: "lines"
```

**Usage:**

```endo
# Recent commits by a specific author
git log |> filter (_.author |> contains "Alice") |> take 5

# List commit messages
git log |> map _.message |> take 10 |> each println
```

### Custom Definition: kubectl

You can create your own definitions for any command:

**`~/.config/endo/definitions/kubectl.endo-output.yml`**:

```yaml
command: "kubectl"
variants:
  - name: "get-pods-json"
    matches:
      - ["get", "pods"]
    priority: 10
    command_to_run: "kubectl get pods -o json {args}"
    parser:
      type: "json"
      format: "array"
      json_path: "$.items"
```

## Built-in Structured Commands

In addition to output recognition files, Endo provides built-in commands that natively
return typed records:

| Command | Record Type | Fields |
|---------|------------|--------|
| `ps` | `ProcessInfo` | `pid`, `ppid`, `user`, `cpu`, `mem`, `command` |
| `ls` | `FileInfo` | `name`, `size`, `mode`, `mtime`, `isDir`, `isSymlink`, `target`, `path` |
| `jobs` | `JobInfo` | `id`, `state`, `command`, `pid` |

These builtins work seamlessly with pipelines:

```endo
# Filter processes by CPU usage
ps |> filter (_.cpu > 5.0) |> sortBy _.cpu

# Find large files
ls |> filter (_.size > 1000000) |> sortBy _.size |> map _.name

# List running background jobs
jobs |> filter (_.state == "Running") |> map _.command
```

## Table Display

When a pipeline produces a list of records and the result is displayed (not piped further),
Endo automatically formats the output as a table:

```endo
ps |> filter (_.cpu > 1.0) |> sortBy _.cpu
# +------+------+-------+------+--------+-----------+
# | pid  | ppid | user  | cpu  | mem    | command   |
# +------+------+-------+------+--------+-----------+
# | 1234 | 1    | alice | 2.5  | 102400 | firefox   |
# | 5678 | 1    | alice | 1.2  | 51200  | code      |
# +------+------+-------+------+--------+-----------+
```

To get plain text output instead, pipe through `toText` or `println`:

```endo
ps |> filter (_.cpu > 1.0) |> each println
```

## Contributing Definitions

The community can share output definitions to expand Endo's structured output support. If
you create a useful definition for a common tool, consider contributing it to the project.
See [Contributing](../contributing.md) for details.
