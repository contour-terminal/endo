---
title: Structured Data Roadmap
description: Roadmap for structured data and system interaction in Endo.
---

# Structured Data Roadmap

This document outlines the roadmap for implementing first-class support for structured data
and system interaction within the Endo shell. It builds upon the F# syntax extensions,
particularly records and union types.

## Vision

The goal is to evolve Endo from a shell that primarily deals with streams of text to one
that can natively understand and manipulate streams of structured objects. This enables
more powerful, reliable, and developer-friendly automation, akin to PowerShell but with a
modern, functional approach.

Instead of parsing text with `awk`, `sed`, and `grep`:

```endo
# Instead of: ps aux | grep 'endo' | awk '{print $2}'
ps |> filter (_.name == "endo") |> map _.pid
```

## Phase 6.1: Core Infrastructure -- Complete

### CoreVM Support for Records and Unions

Discriminated unions and record types are fully implemented in the CoreVM, supporting
efficient creation, manipulation, and reference counting.

### Structured Command Interface

An internal C++ interface (`StructuredCommand`) allows commands to declare that they
produce structured data and advertise the type of their output. Platform-abstracted via
`ProcessProvider` interface for cross-platform support.

### Structured Data Wrapper

Four data source commands for ad-hoc structured parsing:

- `open-json` / `open-csv` -- read from files
- `from-json` / `from-csv` -- read from pipe input

These support inline record type definitions and named type references:

```endo
# Inline type definition
open-json "file.json" as { name: string; age: int }

# Named type reference
type Person = { name: str; age: int }
open-csv "people.csv" as Person

# Pipe-based source
curl api/users | from-json as { name: string; id: int } |> map _.name
```

!!! note
    Output Recognition Files (Phase 6.3a) automate this for pipeline contexts by
    declaratively defining how to parse command output. Explicit wrappers like `from-json`
    remain available for ad-hoc use.

## Phase 6.2: Built-in Structured Commands -- Complete

### `ls`

Returns a stream of `FileInfo` records.

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | File name |
| `size` | int | File size in bytes |
| `mode` | int | File permission bits |
| `mtime` | int | Modification time (epoch seconds) |
| `isDir` | bool | Whether the entry is a directory |

```endo
ls |> filter (_.size > 1000000) |> sortBy _.size |> map _.name
```

### `ps`

Returns a stream of `ProcessInfo` records.

| Field | Type | Description |
|-------|------|-------------|
| `pid` | int | Process ID |
| `ppid` | int | Parent process ID |
| `user` | string | Owner |
| `cpu` | float | CPU usage percentage |
| `mem` | int | Memory usage in KB |
| `command` | string | Command name |

```endo
ps |> filter (_.cpu > 5.0) |> sortBy _.cpu
```

### `jobs`

Returns a stream of `JobInfo` records.

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Job number |
| `state` | str | Running, Stopped, etc. |
| `command` | str | Command string |
| `pid` | int | Process ID |

```endo
jobs |> filter (_.state == "Running") |> map _.command
```

## Phase 6.3: Extensible Command Discovery

### Phase 6.3a: Output Recognition Files -- Complete

Declarative YAML definitions teach Endo how to parse external command output without
modifying the tools themselves.

- YAML definition file format with JSON and fields parser types
- Variant matching by command arguments with priority system
- `command_to_run` override to redirect commands to structured-output flags
- Definition file search paths: `~/.config/endo/definitions/`, system-wide, and bundled
- Pipeline integration via `StructuredPipelineSourceExpr` AST node
- Bundled definitions for `docker ps`, `docker images`, `git log`, `git status`

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

```endo
docker ps |> filter (_.status |> contains "Up") |> map _.names
```

See [Structured Output Recognition](../shell/structured-output.md) for the full
specification.

### Phase 6.3b: Self-Describing Commands -- Planned

For new commands that opt into structured output:

- Discovery via `my-command --endo-schema` convention
- Libraries for C++, Rust, Go, Python to simplify writing structured commands
- Schema caching for performance

## Phase 6.4: Structured Data Pipeline Integration -- Complete

### Record-Aware List Operations

Standard F# higher-order functions work directly with record-typed lists. No special-purpose
verbs are needed:

| Verb Equivalent | F# Function | Example |
|-----------------|-------------|---------|
| `where` | `filter` | `filter (_.name == "endo")` |
| `select` | `map` | `map _.pid` |
| `sort-by` | `sortBy` | `sortBy _.cpu` |
| `group-by` | `groupBy` | `groupBy _.user` |

### Placeholder Lambda Sugar

Parser-level sugar for concise field access:

- `_.field` desugars to `fun __x -> __x.field`
- `_.field == value` desugars to `fun __x -> __x.field == value`
- `_ + 1` desugars to `fun __x -> __x + 1`

### Table Rendering

Lists of records are automatically rendered as tables when displayed:

- Auto-detect column widths with terminal-width-aware shrinking
- Three styles: Bordered (Unicode box-drawing), Compact, Plain
- Auto-style selection: Bordered with color for terminals, Plain for pipes

## Suggested Future Structured Commands

Candidates for built-in structured output:

| Command | Fields |
|---------|--------|
| `df` | `filesystem`, `size`, `used`, `available`, `mountpoint` |
| `netstat`/`ss` | `proto`, `localAddress`, `localPort`, `peerAddress`, `peerPort`, `state`, `pid` |
| `git-log` | `sha`, `author`, `email`, `date`, `message` |
| `docker-ps` | `id`, `image`, `status`, `ports`, `names` |
| `ip-addr` | `interface`, `address`, `netmask`, `family` |
| `history` | `index`, `timestamp`, `command` |
| `env` | `name`, `value` |
