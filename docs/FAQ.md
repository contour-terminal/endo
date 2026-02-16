# Endo Language FAQ

## What is the difference between `|` and `|>`?

This is one of the most common questions for newcomers to Endo. Both operators use the
"pipe" metaphor, but they work at fundamentally different levels.

### `|` — Shell Pipe (Process-level)

The shell pipe connects the **stdout** of one external process to the **stdin** of the next.
It operates on streams of bytes (text) flowing between OS processes.

```fsharp
# Each segment is a separate OS process.
# stdout of `ps aux` feeds into stdin of `grep`, then into `wc`.
ps aux | grep nginx | wc -l
```

This is identical to how pipes work in Bash, Zsh, or any POSIX shell.

### `|>` — Forward Pipe (Value-level)

The forward pipe takes the **result value** of the left-hand side and passes it as the
**last argument** to the function on the right-hand side. No processes are spawned; this
is pure in-memory function application.

```fsharp
# 5 is passed as the argument to `double`.
# Equivalent to: double 5
let double x = x * 2
let result = 5 |> double          # 10
```

Chaining multiple `|>` composes a series of function calls:

```fsharp
let inc x = x + 1
let result = 5 |> double |> inc   # 11
# Equivalent to: inc (double 5)
```

### Side-by-side comparison

| Aspect | `\|` (Shell Pipe) | `\|>` (Forward Pipe) |
|--------|-------------------|----------------------|
| **Operates on** | OS processes (bytes/text) | Values (int, str, list, ...) |
| **Data flow** | stdout &rarr; stdin | value &rarr; last function argument |
| **Runtime cost** | Spawns processes, creates OS pipes | Direct function call (no overhead) |
| **Return value** | Exit code of last command | Return value of last function |
| **Background** | Supports trailing `&` | Not applicable |
| **Context** | Shell statements | F# expressions |

### When to use which?

| Task | Operator | Example |
|------|----------|---------|
| Run external commands | `\|` | `cat log.txt \| grep ERROR \| wc -l` |
| Transform data with functions | `\|>` | `[1; 2; 3] \|> map (fun x -> x * 2)` |
| Pipe to print/println | `\|>` | `42 \|> println` |
| Chain shell tools | `\|` | `find . -name "*.md" \| sort \| head -5` |
| Partial application in a chain | `\|>` | `5 \|> add 10` (equivalent to `add 10 5`) |

### Mixing `|` and `|>` in a single pipeline

One of Endo's most powerful features is that you can transition between the two within
the same expression. A shell pipe produces text, and a forward pipe transforms it with
functions:

```fsharp
# Start with a shell command, then switch to functional processing.
ls -la
| lines                                     # shell pipe: split stdout into lines
|> filter (fun l -> contains l ".rs")       # forward pipe: keep Rust files
|> length                                   # forward pipe: count them
|> fun n -> println $"Found {n} Rust files" # forward pipe: display result
```

Reading this top to bottom:

1. `ls -la` runs the external command (shell).
2. `| lines` pipes its stdout through the `lines` function, producing a `list<str>`.
3. `|> filter (...)` applies a function to keep only matching entries.
4. `|> length` counts the remaining entries.
5. `|> fun n -> ...` receives the count and prints a message.

The boundary between `|` and `|>` is where text becomes structured data. Once you cross
that boundary, you work with typed values instead of raw text.

---

## Can I use lambdas in a `|>` pipeline?

Yes. Wrap the lambda in parentheses or place it at the end:

```fsharp
# Parenthesized lambda
let result = 5 |> (fun x -> x * 2)   # 10

# Lambda at the end of a chain
[1; 2; 3]
|> map (fun x -> x * 2)
|> fun xs -> println xs
```

---

## How does `|>` handle partial application?

When you write `value |> func arg1 arg2`, the piped value is inserted as the **last**
argument. This works naturally with curried functions:

```fsharp
let add x y = x + y

# These are equivalent:
let result = 5 |> add 10     # add 10 5 = 15
let result = add 10 5        # 15
```

This convention matches F# and makes pipelines read left-to-right:

```fsharp
let multiply x y = x * y

10
|> add 5          # add 5 10 = 15
|> multiply 2     # multiply 2 15 = 30
```

---

## What is the `>>` operator?

The `>>` (forward composition) operator composes two functions into a new function,
without applying any value yet. Compare:

```fsharp
# |> applies a value through a chain of functions (eager)
let result = 5 |> double |> inc    # 11

# >> creates a NEW function that is the composition (lazy)
let doubleAndInc = double >> inc
let result = doubleAndInc 5        # 11
```

Use `|>` when you have a value and want to transform it now.
Use `>>` when you want to build a reusable transformation to apply later.

---

## Does `|` capture output when used in a `let` binding?

When a shell command (or pipeline) appears in expression context (e.g., the right-hand
side of a `let`), its stdout is captured as a string:

```fsharp
# Statement context: output goes to terminal
ls -la

# Expression context: output is captured into a variable
let files = & ls -la
let count = & wc -l < README.md
```

The `&` prefix is used to explicitly invoke an external command in expression context.

---

## What is the difference between `& command` and `$(command)`?

Both run a shell command and capture its stdout as a string, but they differ in
**delimiting** and **composability**.

### `$(...)` — Self-delimiting substitution

The parentheses make the boundaries explicit, so `$(...)` composes naturally
with other expressions:

```fsharp
let greeting = $(whoami) + "@" + $(hostname)
let is_clean = $(git status --porcelain) == ""
```

This is the familiar POSIX `$(...)` syntax — it works everywhere a value is
expected.

### `& command` — Greedy shell expression

The `&` prefix switches into shell mode and consumes everything up to the
statement boundary (newline, `;`, `|>`). It's best for full pipelines and
multi-word commands that stand alone:

```fsharp
let log = & tail -n 100 /var/log/syslog | grep error
```

When you need to embed a shell command in a larger expression, `& command`
requires parentheses to delimit it:

```fsharp
let greeting = (& whoami) + "@" + (& hostname)   # parens needed
```

### When to use which?

| Scenario | Preferred | Example |
|----------|-----------|---------|
| Inline in expressions | `$(...)` | `$(whoami) + "@" + $(hostname)` |
| Standalone capture | Either | `let user = $(whoami)` or `let user = & whoami` |
| Full shell pipeline | `& ...` | `let log = & cat file \| grep pattern` |
| Inside if-conditions | `$(...)` | `if $(cmd) == "ok" then ...` |
| String interpolation | `$(...)` | `$"Hello $(whoami)"` |

---

## Can shell pipes run in the background?

Yes. Append `&` to run the entire shell pipeline in the background:

```fsharp
long_running_command | processor | output_handler &
```

This is not available for `|>` forward pipes, since those are synchronous function calls.

---

## How do I filter or transform shell command output with F# functions?

This is one of Endo's core design goals: let shell commands produce data and let
functional code process it. The approach works in three progressively richer layers.

### Layer 1: Capture and process as a string (works today)

You can capture any command's stdout into a string variable with the `&` prefix, then
pipe that string through user-defined functions:

```fsharp
let output = & ps -a
let len = output |> string_length
println (string_of_int len)
```

You can also define your own processing functions and apply them:

```fsharp
let shout (s: str) = s + "!!!"
let result = (& whoami) |> shout
println result                       # "alice!!!"
```

The limitation here is that the entire command output is a single string. To work with
individual lines or fields, you need the next layer.

### Layer 2: Split into lines, filter and map (requires `lines`, `filter`, `map` builtins)

Once the standard library functions from Phase 3 and Phase 7 of the roadmap are
implemented, the bridge becomes seamless. The key function is `lines`, which converts a
multi-line string into a `list<str>`:

```fsharp
# Capture `ps -a` output, split into lines, filter, extract fields.
& ps -a
|> lines                                        # str -> list<str>
|> filter (fun l -> contains l "docker")        # keep lines mentioning docker
|> map (fun l -> words l |> head)               # extract the PID (first column)
|> each println                                 # print each PID
```

Step by step:

1. `& ps -a` runs the shell command and captures its stdout as a single string.
2. `|> lines` splits `"  PID TTY ...\n 1234 pts/0 ...\n ..."` into
   `["  PID TTY ..."; " 1234 pts/0 ..."; ...]`.
3. `|> filter (...)` keeps only the list elements that match a predicate.
4. `|> map (...)` transforms each element (here, extracting the first word).
5. `|> each println` iterates and prints each result.

You can also write the filter as a named function and reuse it:

```fsharp
let mentionsDocker line = contains line "docker"

& docker ps -a
|> lines
|> filter mentionsDocker
|> each println
```

Or use a lambda for a one-off filter:

```fsharp
& docker ps -a
|> lines
|> filter (fun l -> contains l "Up")       # only running containers
|> length                                   # count them
|> fun n -> println $"Running containers: {n}"
```

### Layer 3: Structured commands with typed records (future)

The long-term vision (documented in `ROADMAP-StructuredData.md`) eliminates text parsing
entirely. Commands will produce streams of typed records, and you filter on fields
directly:

```fsharp
# No text parsing needed — `ps` returns list<ProcessInfo> records.
ps
|> filter (_.name == "docker")
|> map _.pid
|> each println
```

This works via two mechanisms:

- **Built-in structured commands**: Endo-native `ps`, `ls`, etc. that return records.
- **Output Recognition Files**: YAML definitions that teach Endo how to parse existing
  CLI tools (e.g., `docker ps --format json`) into records automatically, so
  `docker ps |> filter (_.status |> contains "Up")` just works.

### Summary: the three layers

| Layer | Bridge function | Data type after bridge | Status |
|-------|----------------|----------------------|--------|
| 1. String capture | `& command` | `str` | Available now |
| 2. Text splitting | `lines`, `words`, `split` | `list<str>` | Planned (Phase 3+7) |
| 3. Structured output | Output Recognition / built-in commands | `list<Record>` | Planned (Phase 6) |

The `|>` operator is the same in all three layers — it always passes a value to a
function. What changes is how rich the value is: a raw string, a list of strings, or a
list of typed records.

---

## Windows-Specific Questions

### Why doesn't Ctrl+Z suspend the shell on Windows?

On POSIX systems, Ctrl+Z sends the `SIGTSTP` signal to the foreground process group, which
suspends it and returns control to the parent shell. Windows has no equivalent mechanism:

- There is no `SIGTSTP` signal.
- There are no POSIX-style process groups.
- There is no `tcsetpgrp()` to transfer terminal foreground control.

As a result, pressing Ctrl+Z in Endo on Windows has no effect on the shell itself.

**What still works on Windows:**

- **Ctrl+C** interrupts the foreground child process.
- **bg**, **fg**, and **jobs** manage child processes normally.
- Child processes can be suspended and resumed using Endo's built-in job control, which
  uses `SuspendThread` / `ResumeThread` under the hood.

**Workaround:** Use multiple terminal tabs or windows instead of suspending and resuming
the shell.

See [Platform Differences](shell/platform-differences.md#shell-suspension-ctrlz) for the
full explanation.

---

### How does signal handling work on Windows?

Windows does not have POSIX signals (`SIGCHLD`, `SIGTSTP`, `SIGCONT`, etc.). Endo maps
each signal to its closest Windows API equivalent:

| Action | POSIX | Windows |
|--------|-------|---------|
| Interrupt a process | `SIGINT` | `GenerateConsoleCtrlEvent(CTRL_C_EVENT)` |
| Terminate a process | `SIGTERM` / `SIGKILL` | `TerminateProcess()` |
| Suspend a process | `SIGTSTP` | `SuspendThread()` on all threads |
| Resume a process | `SIGCONT` | `ResumeThread()` on all threads |
| Detect child exit | `SIGCHLD` | `WaitForSingleObject()` polling |

This translation layer is built into Endo's platform abstraction, so shell commands like
`bg`, `fg`, and pipeline management work the same way on both platforms.

See [Platform Differences](shell/platform-differences.md#signal-handling) for details.

---

### Are there any Windows-specific limitations?

Most Endo features work identically on Linux, macOS, and Windows. The known differences
are:

1. **Ctrl+Z does not suspend the shell** -- only child processes can be suspended.
2. **Process substitution (`<(cmd)`, `>(cmd)`) is not yet available** -- this feature
   requires a future implementation using Windows named pipes.
3. **`~username` expansion uses a heuristic** -- on POSIX, user home directories are
   looked up via `getpwnam()`. On Windows, Endo derives the path from the `USERPROFILE`
   parent directory and checks if `C:\Users\<username>` exists.
4. **Environment variable names are case-insensitive** on Windows, matching native Windows
   behavior (`$PATH` and `$Path` refer to the same variable).

See [Platform Differences](shell/platform-differences.md) for the complete comparison.

---

### Does process substitution work on Windows?

Not yet. Process substitution (`<(command)` and `>(command)`) is fully implemented on
Linux and macOS using `/dev/fd` paths, but is not yet available on Windows. A future
release may implement this using Windows named pipes.

As a workaround, capture command output into a temporary file:

```endo
# Instead of: diff <(ls dir1) <(ls dir2)
ls dir1 > /tmp/list1.txt
ls dir2 > /tmp/list2.txt
diff /tmp/list1.txt /tmp/list2.txt
```
