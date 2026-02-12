# Roadmap: Structured Data and System Interaction

This document outlines a roadmap for implementing first-class support for structured data and system interaction within the Endo shell. It builds upon the foundation of the F# style syntax extensions, particularly the planned support for records and union types, as described in the main `ROADMAP.md`.

## Vision

The goal is to evolve Endo from a shell that primarily deals with streams of text to one that can natively understand and manipulate streams of structured objects. This will enable more powerful, reliable, and developer-friendly automation and interactive use cases, akin to PowerShell but with a modern, functional approach.

Instead of parsing text with `awk`, `sed`, and `grep`, users will be able to work with streams of objects directly:

```fsharp
# Instead of: ps aux | grep 'endo' | awk '{print $2}'
ps | where { name == "endo" } | select pid
```

## Proposed Milestone: Milestone 6 - Structured System Interaction

This new milestone will focus on building the infrastructure and features required for structured data support.

### Phase 6.1: Core Infrastructure

**Rationale:** Establish the foundational components within the CoreVM and shell runtime to handle structured data.

**Tasks:**

1.  **Complete CoreVM Support for Records and Unions:**
    *   Finalize the implementation of discriminated unions and record types in the CoreVM, as started in Phase 1.8.
    *   Ensure the VM can efficiently create, manipulate, and garbage-collect these structured types.

2.  **Define the Structured Command Interface:**
    *   Create an internal C++ interface that commands (both built-in and external) can implement to declare that they produce structured data.
    *   This interface should allow a command to advertise the `Type` of the objects it will output (e.g., `list<ProcessInfoRecord>`).

3.  **Implement a Structured Data Wrapper:**
    *   Create a built-in command or a mechanism to wrap existing CLI tools that produce structured text formats like JSON, CSV, or YAML.
    *   This wrapper would be responsible for parsing the text output and transforming it into a stream of Endo records.
    *   Example: `from-json | ...` or `open-csv file.csv | ...`
    *   **Note:** Output Recognition Files (Phase 6.3a) automate this for pipeline contexts by declaratively defining how to parse command output. Explicit wrappers like `from-json` remain available for ad-hoc use where no definition file exists.

### Phase 6.2: Built-in Structured Commands

**Rationale:** Provide a core set of built-in commands that demonstrate the power of structured data and serve as examples for future development.

**Tasks:**

1.  **`ls`:**
    *   Create a new `ls` built-in that outputs a stream of `FileInfo` records.
    *   `FileInfo` record fields: `name: string`, `size: int`, `mode: int`, `mtime: datetime`, `isDir: bool`.

2.  **`ps`:**
    *   Create a `ps` built-in that outputs a stream of `ProcessInfo` records.
    *   `ProcessInfo` record fields: `pid: int`, `ppid: int`, `user: string`, `cpu: float`, `mem: int`, `command: string`.

3.  **`jobs`:**
    *   Rewrite the existing `jobs` built-in to output a stream of `JobInfo` records.
    *   `JobInfo` record fields: `id: int`, `state: JobState`, `command: string`.

### Phase 6.3: Extensible Command Discovery

**Rationale:** Allow the ecosystem of structured commands to grow beyond the built-ins by defining a clear extension mechanism. Two complementary approaches cover both legacy and new commands.

#### Phase 6.3a: Output Recognition Files

**Rationale:** The vast majority of CLI tools will never natively support structured output. Output Recognition Files provide a declarative way to teach Endo how to parse their output, without modifying the tools themselves.

**Tasks:**

1.  **YAML Definition File Format:**
    *   Define the `command.endo-output.yml` format for declaring how to parse command output.
    *   Support three parser types: `json` (preferred for reliability), `table` (column-oriented), and `regex` (fallback for complex formats).
    *   Support variant matching by command arguments with a priority system for selecting the best parser when multiple variants match.
    *   Support `command_to_run` override to redirect commands to structured-output flags (e.g., running `docker ps --format json` when the user types `docker ps`).

2.  **Definition File Search Paths:**
    *   User-specific: `~/.config/endo/definitions/`
    *   System-wide: `/usr/share/endo/definitions/` (or platform equivalent)
    *   Bundled defaults: shipped with Endo for common commands (`docker`, `git`, `ps`, etc.)

3.  **Pipeline Integration:**
    *   When a command appears in a pipeline and a matching definition file exists, automatically parse the output into a stream of structured records.
    *   Select the best-matching variant based on the user's arguments and variant priority.

4.  **Community Definition Library:**
    *   Establish a shareable repository of definition files that users can contribute to and install from.

**Example:** A `docker.endo-output.yml` definition transparently structures `docker ps` output:

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

With this definition, `docker ps | where { status =~ "Up" } | select names` works seamlessly — Endo intercepts the command, runs `docker ps --format json`, and parses the JSON stream into records.

> See [`docs/Structured-Output-Recognition.md`](docs/Structured-Output-Recognition.md) for the full specification, including all parser types and additional examples.

#### Phase 6.3b: Self-Describing Commands

**Rationale:** For new commands that opt into structured output, a self-describing protocol allows richer integration without requiring external definition files.

**Tasks:**

1.  **Discovery Mechanism:**
    *   Define a convention for the shell to discover external commands that support structured output.
    *   A command placed in the `$PATH` that responds to a specific flag, e.g., `my-command --endo-schema`, would print its output schema. The shell can cache this information for performance.

2.  **Developer Tooling:**
    *   Provide libraries (e.g., in C++, Rust, Go, Python) that make it easy for developers to write structured commands for Endo.
    *   These libraries would handle the communication protocol with the shell, data serialization, and type definitions.

### Phase 6.4: Structured Data Manipulation Language

**Rationale:** Equip the shell language with a rich set of operators and commands to work with streams of records.

**Tasks:**

1.  **Implement Core Data Verbs:**
    *   `where { ... }`: Filter a stream of records based on a predicate.
    *   `select { ... }`: Transform each record in a stream (e.g., pick, rename, or compute new fields).
    *   `sort-by { ... }`: Sort a stream of records by one or more fields.
    *   `group-by { ... }`: Group records by a common key.

2.  **Table Rendering:**
    *   A default renderer that takes a stream of records and displays them in a user-friendly table format in the terminal.
    *   The renderer should automatically adjust column widths and handle wide data.

## Suggested System Commands for Structured Output

The following are excellent candidates for commands that would greatly benefit from a structured data model, either as built-ins or as early examples for external command development:

*   **`df`**: Filesystem usage.
    *   Fields: `filesystem: string`, `size: int`, `used: int`, `available: int`, `mountpoint: string`
*   **`netstat`/`ss`**: Network connections.
    *   Fields: `proto: string`, `localAddress: string`, `localPort: int`, `peerAddress: string`, `peerPort: int`, `state: string`, `pid: int`
*   **`git-log`**: Git commit history.
    *   Fields: `sha: string`, `author: string`, `email: string`, `date: datetime`, `message: string`
*   **`docker-ps`**: Docker containers.
    - Fields: `id: string`, `image: string`, `status: string`, `ports: string`, `names: string`
*   **`ip-addr`**: Network interface addresses.
    *   Fields: `interface: string`, `address: string`, `netmask: string`, `family: string` (e.g., "ipv4", "ipv6")
*   **`history`**: Shell command history.
    *   Fields: `index: int`, `timestamp: datetime`, `command: string`
*   **`env`**: Environment variables.
    *   Fields: `name: string`, `value: string`

By following this roadmap, Endo can provide a uniquely powerful and modern environment for system administration and development tasks.
