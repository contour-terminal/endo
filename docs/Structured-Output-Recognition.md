# Structured Output Recognition

This document outlines a proposed system for allowing Endo to understand and parse the output of arbitrary external commands, transforming their text-based output into structured data streams. This is an extension of the ideas presented in `ROADMAP-StructuredData.md`.

## The Challenge

The number of command-line tools is vast. While creating structured built-in commands (`ls`, `ps`) is valuable, the shell's utility would be immensely greater if it could be taught to understand the output of any command, such as `docker`, `git`, `kubectl`, etc., without requiring a new built-in for each.

## The Proposal: Output Recognition Files

We introduce the concept of "Output Recognition Files". These are simple YAML files that declare how to parse the output of a specific command.

*   **File Naming:** Files are named after the command they describe, with a special extension, e.g., `ls.endo-output.yml`, `docker.endo-output.yml`.
*   **Location:** The shell would search for these files in predefined locations, such as a user-specific configuration directory (`~/.config/endo/definitions/`) and a system-wide directory. This allows users to define their own parsers or override default ones.

When the shell is about to execute a command that is part of a pipeline and is expected to produce structured data, it looks for a corresponding definition file. If found, it chooses the best-matching variant and uses its parser to transform the output into a stream of structured records.

### YAML Definition Format

Each file defines one or more parsing variants for a command.

```yaml
# The command this definition is for.
command: "command-name"

# A command can have multiple subcommands or flags that change the output format.
variants:
  # Each variant defines a parser for a specific output format.
  - name: "A unique name for this variant, e.g., 'default' or 'long'"

    # A list of arguments that trigger this variant.
    # The shell will try to match the user's command against this list.
    # '*' can be used as a wildcard.
    matches:
      - ["-l", "*"] # Matches 'ls -l' or 'ls -l /some/path'

    # (Optional) A higher number means higher priority.
    # Used when multiple variants match the same command.
    priority: 10

    # (Optional) If present, the shell will run this command instead of the
    # one typed by the user to get more reliable structured output.
    # {args} is a placeholder for any additional arguments provided by the user.
    command_to_run: "command-name --format json {args}"

    # The parser definition.
    parser:
      # The type of parser to use.
      type: "json" # or "table", "regex"

      # Parser-specific options...
```

## Parser Types

We will start with three powerful parser types.

### 1. `json` Parser (Highest Priority)

This parser is for commands that can output JSON. It is the most reliable method and should be preferred whenever available.

**Options:**

*   `format` (string): Can be `array` (a single JSON array of objects) or `lines` (a stream of newline-delimited JSON objects). Defaults to `lines`.
*   `json_path` (string): An optional [JSONPath](https://goessner.net/articles/JsonPath/) expression to extract data if the root of the document is not the desired array of objects.

### 2. `table` Parser

This is a common case, designed for column-oriented, tabular data. It's less reliable than JSON but very useful for a wide range of legacy tools.

**Options:**

*   `header` (boolean | int): If `true`, the first line is treated as the header. If it's an integer `N`, the parser skips `N` lines before reading the header.
*   `delimiter` (string): The delimiter between columns. Special value `whitespace` treats any sequence of one or more spaces as a single delimiter.
*   `columns` (list): An explicit list of column definitions, used when `header` is `false`.
*   `types` (map): A map of `ColumnName: Type` to provide explicit type hints for columns. Supported types include `string`, `int`, `float`, `size` (e.g., "13.3kB"), `datetime`.

### 3. `regex` Parser

For more complex formats that are not simple tables. This is the most fragile method and should be used as a last resort.

**Options:**

*   `line_regex` (string): A regex used to identify which lines to process.
*   `format_regex` (string): A regex with capture groups. Each capture group corresponds to a column.
*   `columns` (list): A list of column names and types, one for each capture group in `format_regex`.

---

## Examples

### `ls -l`

`ls -l` is notoriously difficult to parse reliably. A regex parser is the only viable option for this legacy format.

**`ls.endo-output.yml`**
```yaml
command: "ls"
variants:
  - name: "long"
    matches:
      - ["-l"]
    parser:
      type: regex
      line_regex: '^[d-]'
      format_regex: '^(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\d+)\s+([A-Za-z]{3}\s+\d+\s+[\d:]+)\s+(.*)$'
      columns:
        - { name: "permissions", type: "string" }
        - { name: "links", type: "int" }
        - { name: "owner", type: "string" }
        - { name: "group", type: "string" }
        - { name: "size", type: "int" }
        - { name: "mtime", type: "string" } # Could be parsed further into a datetime
        - { name: "name", type: "string" }
```

### `ps aux`

This output is table-like, but the last column (`COMMAND`) can contain spaces, making it a good candidate for a `table` parser with a `remaining` flag.

**`ps.endo-output.yml`**
```yaml
command: "ps"
variants:
  - name: "aux"
    matches:
      - ["aux"]
    parser:
      type: table
      header: true
      delimiter: whitespace
      # Define columns to handle the last one correctly.
      columns:
        - { name: "USER", type: "string" }
        - { name: "PID", type: "int" }
        - { name: "%CPU", type: "float" }
        - { name: "%MEM", type: "float" }
        - { name: "VSZ", type: "int" }
        - { name: "RSS", type: "int" }
        - { name: "TTY", type: "string" }
        - { name: "STAT", type: "string" }
        - { name: "START", type: "string" }
        - { name: "TIME", type: "string" }
        - { name: "COMMAND", type: "string", remaining: true } # a special flag
```

### `docker` command (with JSON support)

This example demonstrates how to handle a command with multiple subcommands and different output formats, prioritizing JSON.

**`docker.endo-output.yml`**
```yaml
command: "docker"
variants:
  # 'docker images' variant
  - name: "images-table"
    matches:
      - ["images"]
    priority: 0 # Low priority, fallback
    parser:
      type: table
      header: true
      delimiter: "  " # Two or more spaces
      types:
        SIZE: "size"

  # 'docker ps' variants
  - name: "ps-json"
    matches:
      - ["ps"]
      - ["ps", "-a"]
    priority: 10 # High priority, prefer this one
    # Tell Endo to run a different command to get JSON output
    command_to_run: "docker ps --format json {args}"
    parser:
      type: "json"
      format: "lines" # docker outputs newline-delimited JSON objects

  - name: "ps-table"
    matches:
      - ["ps"]
      - ["ps", "-a"]
    priority: 0 # Low priority, used as a fallback if JSON fails or isn't available
    parser:
      type: table
      header: true
      delimiter: "  "
```
*Note: In the `docker ps` example, if a user types `docker ps -a`, the `ps-json` variant would be chosen. Endo would execute `docker ps --format json -a` and parse the resulting JSON stream.*

## Conclusion

This Output Recognition File system provides a powerful and extensible way to bring the long tail of CLI tools into Endo's structured data world. By supporting and prioritizing native JSON output, we can ensure parsing is both fast and reliable for modern tools. This approach empowers users to define their own parsers and share them, building a community-driven library of structured-aware commands that balances convenience, flexibility, and reliability.
