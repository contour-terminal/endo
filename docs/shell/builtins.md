---
title: Built-in Commands
description: Reference for Endo's built-in shell commands.
---

# Built-in Commands

Endo provides a set of built-in commands that are executed directly by the shell, without
spawning an external process. These builtins handle core shell operations, I/O, and
environment management.

---

## echo

Print arguments to standard output.

**Syntax:**

```
echo [arguments...]
```

**Description:** Writes its arguments to stdout, separated by spaces, followed by a newline.

**Example:**

```endo
echo "Hello, World!"
echo "The value is" $x
```

---

## cd

Change the current working directory.

**Syntax:**

```
cd [directory]
cd -
```

**Description:** Changes the shell's working directory. With no argument, changes to `$HOME`.
Supports tilde expansion (`~`, `~user`). Use `cd -` to switch to the previous working directory (`$OLDPWD`).

**Example:**

```endo
cd /tmp
cd ~
cd ~/projects/endo
cd -          # returns to the previous directory
```

**Tilde expansion for other users:**

```endo
cd ~alice              # go to alice's home directory
cd ~bob/Documents      # go to bob's Documents folder
echo ~alice            # prints the resolved path
```

On POSIX systems, `~username` is resolved via the system user database (`getpwnam`). On
Windows, Endo derives the path from the current user's `USERPROFILE` parent directory --
for example, if `USERPROFILE` is `C:\Users\chris`, then `~alice` resolves to
`C:\Users\alice` (if that directory exists). See
[Platform Differences](platform-differences.md#tilde-expansion-username) for details.

---

## pwd

Print the current working directory.

**Syntax:**

```
pwd
```

**Description:** Outputs the absolute path of the current working directory.

**Example:**

```endo
pwd
# /home/alice/projects
```

---

## exit

Exit the shell.

**Syntax:**

```
exit [code]
```

**Description:** Terminates the shell with the given exit code. Defaults to `0` (success)
if no code is provided.

**Example:**

```endo
exit
exit 1
```

---

## export

Export an environment variable.

**Syntax:**

```
export NAME=VALUE
export NAME
```

**Description:** Sets an environment variable and marks it for export to child processes.
When called with just a name, exports an existing variable.

**Example:**

<!-- endo-no-check -->
```endo
export PATH="/usr/local/bin:$PATH"
export EDITOR=nvim
```

F#-style export is also supported:

```endo
let export MY_VAR = "value"
```

---

## set

Set a shell variable.

**Syntax:**

```
set NAME VALUE
```

**Description:** Sets a shell variable. Unlike `export`, this does not export the variable
to child processes.

**Example:**

```endo
set greeting "hello"
echo $greeting
```

---

## unset

Unset a shell variable.

**Syntax:**

```
unset NAME
```

**Description:** Removes a variable from the shell environment.

**Example:**

```endo
unset MY_VAR
```

---

## read

Read input from the user or a file descriptor.

**Syntax:**

```
read [options] [variable...]
```

**Description:** Reads a line of input and splits it into variables using `$IFS`.

**Flags:**

| Flag | Description |
|------|-------------|
| `-p PROMPT` | Display a prompt string before reading |
| `-r` | Do not interpret backslash escapes |
| `-s` | Silent mode (do not echo input, useful for passwords) |
| `-n COUNT` | Read at most COUNT characters |
| `-t SECONDS` | Timeout after SECONDS (returns failure if exceeded) |
| `-d DELIM` | Use DELIM as the line delimiter instead of newline |

**Example:**

```endo
read -p "Enter your name: " name
echo "Hello, $name!"

read -s -p "Password: " password

read -n 1 -p "Continue? (y/n) " answer
```

---

## true

Return a successful (zero) exit code.

**Syntax:**

```
true
```

**Description:** Always returns exit code `0`. Useful in conditionals and loops.

**Example:**

```endo
while true do
    echo "loop"
    break
end
```

---

## false

Return a failure (non-zero) exit code.

**Syntax:**

```
false
```

**Description:** Always returns exit code `1`. Useful in conditionals.

**Example:**

```endo
if false then println "unreachable"
```

---

## which

Locate a command in `$PATH`.

**Description:** `which` is a dual-mode builtin that searches the system `$PATH` for a given
command. In **shell mode** (bare argument), it prints the resolved path to stdout and sets the
exit code. In **F# mode** (quoted or parenthesized argument), it returns an `Option<string>`
value suitable for pattern matching, pipelines, and functional composition.

The mode is determined by syntax: bare identifiers like `which git` use shell mode, while
quoted strings like `which "git"` or parenthesized expressions like `which (name)` use F# mode.

### Shell form

**Syntax:**

```
which command
```

Searches `$PATH` for *command* and prints its absolute path to stdout. Returns exit code `0`
on success or `1` if the command is not found.

**Examples:**

```endo
which git
# /usr/bin/git

which nonexistent
# (returns exit code 1)

# Use in a conditional
match which "cargo" with
| Some _ -> println "Rust toolchain available"
| None -> ()
```

### F# form

**Syntax:**

```
which "command"
which (expr)
```

**Return type:** `Option<string>`

Returns `Some path` if the program is found in `$PATH`, or `None` otherwise. The returned
path is the fully resolved absolute path to the executable.

**Examples:**

<!-- endo-no-check -->
```endo
# Pattern match on the result
match which "git" with
| Some path -> println path
| None -> println "git not found"

# With default operator
let gitPath = which "git" ?| "/usr/bin/git"

# In a pipeline
"ls" |> which |> fun opt -> match opt with | Some p -> println p | None -> println "not found"

# Bind to a variable
let result = which "cargo"
```

!!! tip
    Use the shell form (`which git`) for quick interactive lookups.
    Use the F# form (`which "git"`) when you need to branch on the result or compose it
    into a larger expression.

---

## cat

Concatenate and display files.

**Syntax:**

```
cat [OPTIONS] [FILE...]
```

**Description:** Reads files sequentially and writes their contents to standard output. If
no files are given (or when FILE is `-`), reads from standard input. When output goes to a
terminal, syntax highlighting is applied automatically based on the file name.

Highlighting is supported for C/C++ (and C-family languages such as JavaScript, TypeScript,
Rust, Go), Python, Bash, CMake, Markdown, JSON, YAML, x86 assembly, Git diffs, PowerShell
(`.ps1`, `.psm1`, `.psd1`), Windows CMD/batch (`.cmd`, `.bat`), XML and XML-based dialects
(`.xml`, `.props`, `.csproj`, `.targets`, `.vcxproj`, `.xaml`, `.svg`, …), INI (`.ini`), and
Endo (`.endo`). A few well-known files are recognized by name rather than extension: `.clang-format`,
`.clang-tidy`, and `.endo-format` are highlighted as YAML, and `.editorconfig` as INI.

**Options:**

| Option | Long form | Description |
|--------|-----------|-------------|
| `-n` | `--number` | Number all output lines |
| `-b` | `--number-nonblank` | Number non-blank output lines (overrides `-n`) |
| `-s` | `--squeeze-blank` | Suppress repeated empty output lines |
| `-E` | `--show-ends` | Display `$` at end of each line |
| `-T` | `--show-tabs` | Display TAB characters as `^I` |
| `-A` | `--show-all` | Equivalent to `-ET` |
| `-r` | `--range START..END` | Show only lines in the given range |
| `-h` | `--help` | Display help and exit |

**Range syntax:**

- `3..7` — show lines 3 through 7
- `..5` — show lines 1 through 5
- `10..` — show from line 10 to the end of the file

When `-n` or `-b` is combined with `--range`, line numbers reflect the original file
positions (not re-numbered from 1).

**Examples:**

```endo
cat README.md
cat file1.txt file2.txt > combined.txt
echo "hello" | cat
cat -n script.sh
cat --range 10..20 main.cpp
cat -nr 5..15 data.txt
```

---

## mkdir

Create directories.

**Syntax:**

```
mkdir [-pv] [--parents] [--verbose] [--] directory...
```

**Description:** Creates the specified directories. By default, fails if the directory already
exists or if parent directories are missing.

**Options:**

| Option | Description |
|---|---|
| `-p`, `--parents` | Create parent directories as needed; no error if the directory already exists |
| `-v`, `--verbose` | Print a message for each created directory |
| `--` | End of options; treat subsequent arguments as directory names |

**Examples:**

```endo
mkdir mydir
mkdir -p path/to/nested/dir
mkdir -pv project/src project/tests
mkdir -- -starts-with-dash
```

---

## cp

Copy files and directories.

**Syntax:**

```
cp [-rfnvR] [--recursive] [--force] [--no-clobber] [--verbose] [--] source... dest
```

**Description:** Copies SOURCE to DEST, or multiple SOURCE(s) to an existing DIRECTORY. By default,
overwrites existing destination files. Copying a directory requires the `-r` flag.

**Options:**

| Option | Description |
|---|---|
| `-r`, `-R`, `--recursive` | Copy directories recursively |
| `-f`, `--force` | Force overwrite; remove destination if needed |
| `-n`, `--no-clobber` | Do not overwrite existing files |
| `-v`, `--verbose` | Print each file as it is copied |
| `--` | End of options; treat subsequent arguments as file names |

**Examples:**

```endo
cp file.txt backup.txt
cp -r srcdir dstdir
cp -rv project/ /tmp/project-backup/
cp -n important.txt dest/
cp -- -starts-with-dash.txt dest.txt
cp a.txt b.txt c.txt target-directory/
```

---

## mv

Move or rename files and directories.

**Syntax:**

```
mv [-fnvi] [--force] [--no-clobber] [--verbose] [--interactive] [--] source... dest
```

**Description:** Moves SOURCE to DEST, or multiple SOURCE(s) to an existing DIRECTORY.
If the destination is on a different filesystem, the file is copied and the source is removed.
By default, overwrites existing destination files without prompting.

**Options:**

| Option | Description |
|---|---|
| `-f`, `--force` | Do not prompt before overwriting |
| `-n`, `--no-clobber` | Do not overwrite existing files |
| `-v`, `--verbose` | Print each file as it is moved |
| `-i`, `--interactive` | Prompt before overwriting |
| `--` | End of options; treat subsequent arguments as file names |

<!-- endo-no-check -->
**Examples:**

```endo
mv file.txt renamed.txt
mv file.txt /tmp/
mv -v old.txt new.txt
mv -n important.txt dest/
mv -- -starts-with-dash.txt dest.txt
mv a.txt b.txt c.txt target-directory/
```

---

## rm

Remove files and directories.

**Syntax:**

```
rm [OPTIONS] FILE...
```

**Description:** Remove (unlink) the specified files. By default, does not remove directories.
Use `-r` to remove directories and their contents recursively. Refuses to recursively remove
`/`, `.`, or `..` as a safety measure.

**Options:**

| Option | Description |
|---|---|
| `-f`, `--force` | Ignore nonexistent files, never prompt |
| `-i` | Prompt before every removal |
| `-r`, `-R`, `--recursive` | Remove directories and their contents recursively |
| `-d`, `--dir` | Remove empty directories |
| `-v`, `--verbose` | Explain what is being done |
| `--` | End of options |
| `-h`, `--help` | Display help |

**Examples:**

```endo
rm file.txt
rm -f nonexistent.txt
rm -rf build/
rm -rv old-project/
rm -- -starts-with-dash.txt
```

---

## find

Search for files in a directory hierarchy.

**Syntax:**

```
find [PATH...] [EXPRESSION]
```

**Description:** Recursively searches for files and directories matching the given criteria.
If no path is given, the current directory is used. If no expression is given, all files are
matched.

**Options:**

| Option | Description |
|---|---|
| `-maxdepth N` | Descend at most N levels |
| `-mindepth N` | Do not apply tests at levels less than N |
| `-print0` | Print entries separated by null instead of newline |
| `-h`, `--help` | Display help |

**Predicates:**

| Predicate | Description |
|---|---|
| `-name PATTERN` | Match filename against glob pattern |
| `-iname PATTERN` | Like `-name` but case-insensitive |
| `-path PATTERN` | Match full path against glob pattern |
| `-ipath PATTERN` | Like `-path` but case-insensitive |
| `-type TYPE` | Match file type: `f` (file), `d` (directory), `l` (symlink) |
| `-size [+\|-]N[c\|k\|M\|G]` | Match file size (c=bytes, k=KiB, M=MiB, G=GiB) |
| `-mtime [+\|-]N` | Match modification time in 24-hour periods |
| `-newer FILE` | Match files newer than FILE |
| `-empty` | Match empty files or directories |

**Operators:**

| Operator | Description |
|---|---|
| `-a`, `-and` | Logical AND (implicit between predicates) |
| `-o`, `-or` | Logical OR |
| `-not`, `!` | Logical NOT |
| `( expr )` | Group expressions |

**Examples:**

<!-- endo-no-check -->
```endo
find . -name "*.cpp"
find src -type f -name "*.hpp"
find . -type f -size +1M
find . -name "*.o" -o -name "*.tmp"
find /tmp -maxdepth 2 -empty
```

---

## grep

Search for patterns in files.

**Syntax:**

```
grep [OPTIONS] PATTERN [FILE...]
```

**Description:** Searches for lines matching a regular expression pattern. If no files are
specified, reads from standard input. Returns exit code 0 if matches are found, 1 if no
matches are found, and 2 if errors occurred.

**Pattern options:**

| Option | Description |
|---|---|
| `-e PATTERN` | Use PATTERN for matching (repeatable for multiple patterns) |
| `-F` | Interpret PATTERN as fixed strings, not regex |
| `-E` | Interpret PATTERN as extended regex (default) |
| `-i` | Ignore case distinctions |
| `-w` | Match whole words only |
| `-x` | Match whole lines only |

**Output options:**

| Option | Description |
|---|---|
| `-c` | Print only a count of matching lines per file |
| `-l` | Print only names of files with matches |
| `-L` | Print only names of files without matches |
| `-n` | Prefix each line with its line number |
| `-H` | Print the filename for each match |
| `-h` | Suppress the filename prefix |
| `-o` | Print only the matching parts of lines |
| `-v` | Invert match — select non-matching lines |
| `-q` | Quiet mode — no output, exit code only |
| `-s` | Suppress error messages about missing files |

**Context options:**

| Option | Description |
|---|---|
| `-A NUM` | Print NUM lines of trailing context after each match |
| `-B NUM` | Print NUM lines of leading context before each match |
| `-C NUM` | Print NUM lines of context (before and after) |

**File selection:**

| Option | Description |
|---|---|
| `-r`, `-R` | Recurse into directories |
| `-m NUM` | Stop after NUM matches per file |
| `-I` | Skip binary files |
| `--include=GLOB` | Search only files matching GLOB |
| `--exclude=GLOB` | Skip files matching GLOB |
| `--exclude-dir=DIR` | Skip directories matching DIR |
| `--color=MODE` | Colorize output (`auto`, `always`, `never`) |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
grep "TODO" src/*.cpp
grep -rn "error" src/
grep -i "pattern" file.txt
grep -c "match" *.log
grep -rl "deprecated" --include="*.hpp" src/
grep -v "^#" config.txt
```

---

## sleep

Wait for a specified duration.

**Syntax:**

```
sleep seconds
```

**Description:** Pauses execution for the given number of seconds. Supports decimal values.

**Example:**

```endo
sleep 1
sleep 0.5
echo "done"
```

---

## timeout

Run a command with a time limit.

**Syntax:**

```
timeout [OPTIONS] DURATION COMMAND [ARG...]
```

**Description:** Runs a command and terminates it if it does not complete within the given
duration. By default, sends `SIGTERM` on timeout. Use `-k` to send `SIGKILL` after a grace
period if the command does not respond to the initial signal.

**Duration format:** A number with optional suffix: `s` (seconds, default), `m` (minutes),
`h` (hours), `d` (days). Decimal values are supported (e.g. `1.5m`).

**Options:**

| Option | Description |
|---|---|
| `-s SIGNAL`, `--signal=SIGNAL` | Signal to send on timeout (default: `TERM`) |
| `-k DURATION`, `--kill-after=DURATION` | Send `SIGKILL` after grace period if still running |
| `--preserve-status` | Return the command's exit status instead of 124 on timeout |
| `--foreground` | Don't create a separate process group |
| `-v`, `--verbose` | Diagnose to stderr when signal is sent |
| `-h`, `--help` | Show help |

**Exit codes:**

| Code | Meaning |
|---|---|
| 124 | Command timed out |
| 125 | `timeout` itself failed |
| 126 | Command found but not executable |
| 127 | Command not found |
| 128+N | Command was killed by signal N |

**Examples:**

<!-- endo-no-check -->
```endo
timeout 5 sleep 100
timeout 1.5m long-running-task
timeout -s KILL 10 command
timeout -k 2 30 long-running-cmd
```

---

## kill

Send signals to processes or jobs.

**Syntax:**

```
kill [-SIGNAL | -s SIGNAL] PID|%JOB ...
kill -l
```

**Description:** Sends a signal to one or more processes by PID, or to background jobs by
job ID. The default signal is `SIGTERM` (15). Use `%N` to refer to job number N from the
job table (see `jobs`).

**Options:**

| Option | Description |
|---|---|
| `-SIGNAL` | Signal to send by name or number (e.g. `-9`, `-TERM`, `-SIGKILL`) |
| `-s SIGNAL` | Signal to send (POSIX style) |
| `-l` | List available signal names |
| `-h`, `--help` | Show help |

**Common signals:**

| # | Name | Description |
|---|---|---|
| 1 | `HUP` | Hangup |
| 2 | `INT` | Interrupt (Ctrl+C) |
| 9 | `KILL` | Kill (cannot be caught) |
| 15 | `TERM` | Terminate (default) |
| 19 | `STOP` | Stop (cannot be caught) |

**Examples:**

<!-- endo-no-check -->
```endo
kill 1234
kill -9 1234
kill -TERM 1234
kill %1
kill -s USR1 5678
kill -l
```

---

## pgrep

Print PIDs of processes matched by name or command-line pattern.

**Syntax:**

```
pgrep [OPTIONS] PATTERN
```

**Description:** Lists the process IDs of all running processes whose command name matches
PATTERN, an ECMAScript regular expression. By default a substring search is performed and
one PID is printed per line. Exits with status 0 if at least one process matched, 1 if
none did.

**Options:**

| Option | Description |
|---|---|
| `-f` | Match PATTERN against the full command line |
| `-x` | Require an exact (anchored) match |
| `-i` | Case-insensitive pattern match |
| `-v` | Invert the match: select non-matching processes |
| `-c` | Print only the count of matched processes |
| `-l` | Print the process name along with the PID |
| `-n` | Match only the newest (highest PID) process |
| `-o` | Match only the oldest (lowest PID) process |
| `-u USER[,USER]` | Only match processes owned by the listed users |
| `-d DELIM` | Delimiter between printed PIDs (default: newline) |
| `-h`, `--help` | Show help |

**Portability:** Matching runs against the platform's most reliable command field — the
full `argv[0]` path on Linux, the (possibly truncated) command name on macOS, and the
executable name (including `.exe`) on Windows. `-f` is accepted for CLI compatibility but
targets the same field on all platforms.

**Examples:**

<!-- endo-no-check -->
```endo
pgrep sleep
pgrep -x sleep
pgrep -l ssh
pgrep -c -u alice bash
pgrep -d , sleep
```

---

## pidof

Print PIDs of running processes matching program names.

**Syntax:**

```
pidof [OPTIONS] PROGRAM...
```

**Description:** Looks up the process IDs of all running processes whose program name
equals one of the given PROGRAM names, and prints them space-separated on a single line,
newest (highest PID) first. Unlike `pgrep`, matching is exact, not pattern-based. Exits
with status 0 if at least one PID was found, 1 otherwise.

**Options:**

| Option | Description |
|---|---|
| `-s` | Single shot: print at most one PID |
| `-q` | Quiet: print nothing, only set the exit status |
| `-S SEP`, `--separator SEP` | Separator between printed PIDs (default: space) |
| `-d SEP` | Alias for `-S` (sysvinit compatibility) |
| `-o PID[,PID]` | Omit the listed PIDs from the result (repeatable) |
| `-h`, `--help` | Show help |

**Portability:** A PROGRAM name matches the process's command field verbatim or by its
path basename. On Linux, the command field is the full `argv[0]` path (so both
`pidof sleep` and `pidof /usr/bin/sleep` work); on macOS it is the (possibly truncated)
command name; on Windows the comparison is case-insensitive and a trailing `.exe` is
ignored (`pidof notepad` matches `notepad.exe`).

**Examples:**

<!-- endo-no-check -->
```endo
pidof sleep
pidof -s sleep
pidof -q sleep
pidof -d , sleep
pidof -o 1234 sleep
```

---

## jobs

List background jobs.

**Syntax:**

```
jobs
```

**Description:** Displays all background and stopped jobs with their job number, state, and
command. The current job is marked with `+`, the previous job with `-`.

**Job states:**

| State | Description |
|---|---|
| Running | Job is currently executing |
| Stopped | Job was suspended (e.g. by Ctrl+Z) |
| Done | Job completed normally |
| Terminated | Job was killed by a signal |

**Example:**

<!-- endo-no-check -->
```endo
sleep 100 &
sleep 200 &
jobs
# [1]+ Running   sleep 100 &
# [2]- Running   sleep 200 &
```

---

## fg

Bring a background job to the foreground.

**Syntax:**

```
fg [job_id]
```

**Description:** Resumes a stopped or background job in the foreground and waits for it to
complete. If no job ID is given, uses the current job (marked with `+` in `jobs` output).

**Examples:**

<!-- endo-no-check -->
```endo
sleep 100 &
fg          # bring the current job to foreground
fg 2        # bring job 2 to foreground
```

---

## bg

Resume a stopped job in the background.

**Syntax:**

```
bg [job_id]
```

**Description:** Resumes a stopped job in the background without waiting for it to complete.
The job must be in the Stopped state (e.g. suspended with Ctrl+Z). If no job ID is given,
uses the current job.

**Examples:**

<!-- endo-no-check -->
```endo
# After pressing Ctrl+Z to suspend a job:
bg          # resume current job in background
bg 1        # resume job 1 in background
```

---

## wait

Wait for background jobs to complete.

**Syntax:**

```
wait [job_id]
```

**Description:** Waits for one or all background jobs to finish and returns their exit status.
If a job ID is given, waits for that specific job. If no job ID is given, waits for all
running background jobs.

**Examples:**

<!-- endo-no-check -->
```endo
sleep 5 &
wait        # wait for all background jobs
echo "all done"

sleep 10 &
wait 1      # wait for job 1 specifically
echo $?     # prints the job's exit code
```

---

## fetch

Perform HTTP requests.

**Syntax:**

```
fetch URL
```

**Description:** Makes an HTTP GET request to the given URL and outputs the response body
to stdout.

**Example:**

```endo
fetch https://api.github.com/zen
```

!!! note
    The `fetch` builtin currently supports basic GET requests. Additional HTTP methods and
    options are planned for a future release.

---

## bind

Configure key bindings.

**Syntax:**

```
bind                    # List all bindings
bind -l, --list         # List all bindings (same as no args)
bind KEY ACTION         # Bind a key to an action
bind -r, --remove KEY   # Remove a binding
bind --reset            # Reset to default bindings
bind -h, --help         # Show available actions and key format
```

**Description:** Manages the shell's key bindings at runtime. Changes take effect
immediately. Keys are specified as modifier+key combinations (e.g. `ctrl+a`, `alt+shift+f`).

**Modifiers:** `ctrl`, `alt` (or `meta`), `shift`, `super` (or `win`, `cmd`)

**Keys:** `a`–`z`, `enter`, `backspace`, `delete`, `tab`, `escape`, `up`, `down`, `left`,
`right`, `home`, `end`, `pageup`, `pagedown`, `insert`, `space`, `f1`–`f12`

**Example:**

```endo
# List all bindings
bind

# Bind Ctrl+Y to yank (paste from kill ring)
bind ctrl+y yank

# Remove a binding
bind -r ctrl+y

# Reset to defaults
bind --reset

# Show available actions grouped by category
bind --help
```

**Available actions:**

| Category | Actions |
|----------|---------|
| Movement | `move-forward-char`, `move-backward-char`, `move-forward-word`, `move-backward-word`, `move-to-line-start`, `move-to-line-end`, `move-to-buffer-start`, `move-to-buffer-end`, `move-up`, `move-down`, `smart-move-to-line-start`, `smart-move-to-line-end` |
| Editing | `delete-char-backward`, `delete-char-forward`, `delete-word`, `delete-word-backward`, `delete-big-word-backward`, `kill-to-end`, `kill-to-start`, `transpose` |
| Undo/Redo | `undo`, `redo` |
| Kill Ring | `yank`, `yank-pop` |
| Selection | `select-all` |
| Clipboard | `cut`, `copy`, `paste` |
| Control | `submit`, `abort`, `insert-newline` |
| History | `history-prev`, `history-next` |

See [Configuration: Key Bindings](configuration.md#key-bindings) for the full list of default
bindings.

---

## history

Display or manage command history.

**Syntax:**

```
history [N | search PATTERN | clear]
```

**Description:** Displays the command history with numbered entries. When called without
arguments, lists all entries. Supports viewing a subset, searching, and clearing.

**Subcommands:**

| Subcommand | Description |
|---|---|
| *(none)* | List all history entries, numbered |
| `N` | List the last N entries |
| `search PATTERN` | Search entries by prefix |
| `clear` | Clear all history |
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
history
history 20
history search git
history clear
```

---

## env

Get an environment variable (F# style).

**Syntax (F# expression):**

```endo
env "VARIABLE_NAME"
```

**Description:** Returns the value of an environment variable as an `option<str>`. Returns
`Some value` if the variable is set, or `None` if it is not.

**Example:**

<!-- endo-no-check -->
```endo
# Pattern match on the result
match (env "HOME") with
| Some dir -> println $"Home directory: {dir}"
| None     -> println "HOME is not set"

# Use with Option default
let editor = (env "EDITOR") ?| "vi"
println $"Using editor: {editor}"

# Use with the ? operator in a function
let getHome () =
    let home = (env "HOME")?
    Ok home
```

!!! tip
    For shell-style access to environment variables, use `$VAR` or `${VAR}` substitution
    syntax instead. The `env` function is designed for F# expressions where you want
    type-safe `Option` handling.

---

## source-env

Import environment variables from an external script.

**Syntax:**

```
source-env <script-path>
source-env <script-path> <args...>
```

**Description:** Runs an external script in its native interpreter, captures the resulting
environment variables, and imports any new or changed variables into the current Endo shell
session. The interpreter is selected based on the script's file extension.

**Supported script types:**

| Extension       | Interpreter                    | Platforms            |
|-----------------|--------------------------------|----------------------|
| `.bat`, `.cmd`  | `cmd.exe`                      | Windows              |
| `.ps1`          | `pwsh` or `powershell.exe`     | All (cross-platform) |
| `.sh`           | `bash`                         | All                  |

For `.ps1` scripts, `pwsh` (PowerShell Core) is tried first. On Windows, `powershell.exe`
(Windows PowerShell 5.x) is used as a fallback. On other platforms, `pwsh` must be installed.

Only variables that are **new** or **changed** compared to the current environment are
imported. Existing variables that the script does not modify are left unchanged.

**Example:**

<!-- endo-no-check -->
```endo
# Set up MSVC toolchain on Windows (PowerShell script)
source-env "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1"

# Set up MSVC toolchain on Windows (batch file with architecture)
source-env "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" "x64"

# Source a bash environment script
source-env /opt/tools/setup-env.sh

# Source a PowerShell script on Linux (requires pwsh)
source-env ./setup-toolchain.ps1
```

**Return value:** The exit code of the sourced script (0 on success).

---

## source

Execute a script in the current shell context.

**Syntax:**

```
source FILE [ARGS...]
. FILE [ARGS...]
```

**Description:** Reads and executes commands from FILE in the current shell environment.
Unlike running a script as a subprocess, variables, functions, and other state changes
made by the sourced script persist after it completes. The `.` command is a POSIX-compatible
alias for `source`.

**Options:**

| Option | Description |
|---|---|
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
source ~/.config/endo/utils.endo
. ./setup-project.endo
```

!!! tip
    Use `source` for Endo scripts (`.endo` files) that should modify the current shell
    environment. For importing environment variables from external shell scripts (bash, zsh,
    PowerShell), use [`source-env`](#source-env) instead.

!!! note
    You can also run `.endo` files directly in shell mode by typing their path:
    `./script.endo` or `path/to/script.endo`. Endo detects the `.endo` extension and
    executes the file in-process instead of spawning it as an external command.

---

## run_script

Execute an `.endo` script file in the current shell context (F# callable).

**Syntax:**

```endo
run_script "path/to/file.endo"
```

**Description:** Reads and executes an `.endo` script file in the current shell context,
similar to `source`. Unlike `source`, `run_script` is an F# function and can be called
from within expressions, pipelines, and function bodies. Returns the exit code as an integer.

Module imports within the executed script resolve relative to the script's location.

**Examples:**

<!-- endo-no-check -->
```endo
# Execute a script
run_script "setup.endo"

# Check exit code
let result = run_script "tests/validate.endo"
if result <> 0 then println "validation failed"
```

---

## rand

Generate a random integer.

**Syntax:**

```
rand
rand <min> <max>
```

**Description:** Generates a random integer. With no arguments, returns a random positive
integer greater than zero. With two arguments, returns a random integer in the inclusive
range [min, max].

**Example:**

```endo
# Random positive integer
let n = rand
print n

# Random integer between 1 and 6 (inclusive)
let roll = rand 1 6
print $"You rolled a {roll}"

# Use in a pipeline
rand 1 100 |> fun n -> print $"Random: {n}"
```

---

## dirconfig

Manage directory configuration trust entries.

**Syntax:**

```
dirconfig <subcommand> [path]
```

**Description:** Endo supports per-directory configuration files (`.local-env.endo`) that are
sourced when you enter a directory. Because these files can execute arbitrary commands, they
require explicit trust approval. The `dirconfig` command manages this trust database.

If no path is given, defaults to `.local-env.endo` in the current directory.

**Subcommands:**

| Subcommand | Description |
|---|---|
| `allow [path]` | Trust and allow a directory config |
| `deny [path]` | Deny a directory config |
| `revoke [path]` | Remove the trust entry for a directory config |
| `list` | List all registered directory config trust entries |
| `reload` | Reload all directory configs |

**Options:**

| Option | Description |
|---|---|
| `-h`, `--help` | Show help |

**Examples:**

<!-- endo-no-check -->
```endo
dirconfig allow                    # trust .local-env.endo in current dir
dirconfig allow ~/project/.local-env.endo
dirconfig deny /tmp/.local-env.endo
dirconfig list
dirconfig revoke ~/old-project/.local-env.endo
dirconfig reload
```

---

## whoami

Print the current username.

**Syntax:**

```
whoami
```

**Description:** Prints the effective username of the current user. Cross-platform: uses
`getpwuid(geteuid())` on POSIX and `GetUserNameA()` on Windows.

**Example:**

<!-- endo-no-check -->
```endo
whoami
# alice
```

---

## hostname

Print the system hostname.

**Syntax:**

```
hostname
```

**Description:** Prints the network hostname of the current machine.

**Example:**

<!-- endo-no-check -->
```endo
hostname
# my-laptop
```

---

## date

Print or format the current date and time.

**Syntax:**

```
date [OPTIONS]
date +FORMAT
```

**Description:** Displays the current date and time. Supports custom format strings
(strftime syntax), epoch timestamps, ISO 8601 output, and UTC mode.

**Options:**

| Option | Description |
|---|---|
| `-u`, `--utc` | Use UTC instead of local time |
| `--epoch` | Print seconds since Unix epoch |
| `--iso` | Print in ISO 8601 format |
| `-f FMT`, `--format FMT` | Use custom strftime format |
| `-d STRING`, `--date STRING` | Display given date (supports `@EPOCH`) |
| `-h`, `--help` | Display help |

**Format specifiers:** `%Y` (year), `%m` (month), `%d` (day), `%H` (hour), `%M` (minute),
`%S` (second), `%A` (weekday), `%B` (month name), `%Z` (timezone).

**Examples:**

<!-- endo-no-check -->
```endo
date
date --epoch
date --iso
date +%Y-%m-%d
date -u --iso
date -d @1700000000
```

---

## cal

Display a colorful calendar for a month or year.

**Syntax:**

```
cal [OPTIONS]
cal [OPTIONS] YEAR
cal [OPTIONS] MONTH YEAR
```

**Description:** Renders a monthly or yearly calendar. With no arguments, shows the
current month with today's date highlighted. `MONTH YEAR` selects a specific month
(`MONTH` is 1-12); a single positional argument is treated as a year from 1-9999 and
renders all 12 months in a 3×4 grid.

When stdout is a terminal and `NO_COLOR` is not set, `cal` emits ANSI color: today's
date is highlighted, the month/year header is bold, and weekend columns are tinted.
Color is suppressed automatically when output is piped or when `--no-color` is given.

**Options:**

| Option | Description |
|---|---|
| `-3`, `--three` | Show previous, current, and next month side-by-side |
| `-y`, `--year` | Show the entire current year |
| `-m`, `--monday` | Start the week on Monday (ISO 8601, default) |
| `-s`, `--sunday` | Start the week on Sunday |
| `-n`, `--no-color` | Disable colorized output even on a terminal |
| `-h`, `--help` | Display help |

**Examples:**

```endo
cal
cal 4 2026
cal 2026
cal -3
cal -s 4 2026
cal --no-color 2026 > year.txt
```

---

## uname

Print system information.

**Syntax:**

```
uname [OPTIONS]
```

**Description:** Prints system information. With no options, prints the kernel name.

**Options:**

| Option | Description |
|---|---|
| `-s` | Print kernel name (default) |
| `-n` | Print network node hostname |
| `-r` | Print kernel release |
| `-m` | Print machine hardware name |
| `-a` | Print all information |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
uname
# Linux
uname -a
# Linux my-laptop 6.8.0-generic x86_64
uname -sm
# Linux x86_64
```

---

## nproc

Print the number of available processing units.

**Syntax:**

```
nproc [OPTIONS]
```

**Description:** Prints the number of processing units (CPU cores/threads) available on the
system. Cross-platform: uses `std::thread::hardware_concurrency()` on all supported platforms
(Linux, macOS, Windows). Compatible with Linux's `/usr/bin/nproc`.

**Options:**

| Option | Description |
|---|---|
| `--all` | Print the number of installed processors (default) |
| `--ignore=N` | Exclude N processing units (result is at least 1) |
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
nproc
# 8

nproc --ignore=2
# 6

# Use in command substitution
let cores = $(nproc)
```

---

## basename

Strip directory and suffix from pathnames.

**Syntax:**

```
basename PATH [SUFFIX]
```

**Description:** Strips the directory portion from a pathname, leaving just the filename.
If a SUFFIX is provided and the filename ends with it, the suffix is removed.

**Examples:**

```endo
basename /usr/local/bin/endo
# endo
basename /usr/local/file.txt .txt
# file
basename /usr/local/
# local
```

---

## dirname

Strip last component from pathname.

**Syntax:**

```
dirname PATH
```

**Description:** Strips the last component from a pathname, returning the directory portion.
Returns `.` if the path contains no directory separators.

**Examples:**

```endo
dirname /usr/local/bin/endo
# /usr/local/bin
dirname file.txt
# .
dirname /file.txt
# /
```

---

## realpath

Resolve to absolute canonical path.

**Syntax:**

```
realpath PATH...
```

**Description:** Resolves each PATH to an absolute canonical path by expanding symlinks and
removing `.` and `..` components. Fails if the path does not exist.

**Examples:**

<!-- endo-no-check -->
```endo
realpath ./README.md
# /home/alice/projects/endo/README.md
realpath ../other-project
```

---

## touch

Create files or update timestamps.

**Syntax:**

```
touch [OPTIONS] FILE...
```

**Description:** Updates the modification timestamp of each FILE. Creates the file if it
does not exist (unless `-c` is given).

**Options:**

| Option | Description |
|---|---|
| `-c`, `--no-create` | Do not create files that don't exist |
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
touch newfile.txt
touch -c existing-only.txt
touch a.txt b.txt c.txt
```

---

## ln

Create links between files.

**Syntax:**

```
ln [OPTIONS] TARGET                 # link in the current directory, named after TARGET
ln [OPTIONS] TARGET LINK_NAME       # create LINK_NAME as a link to TARGET
ln [OPTIONS] TARGET... DIRECTORY    # create links to each TARGET inside DIRECTORY
```

**Description:** Creates a link to TARGET. By default, creates a hard link; use `-s` for a
symbolic link.

Operand handling follows GNU `ln`:

- With a single TARGET, the link is created in the current directory, named after TARGET's
  basename (e.g. `ln -s build/clang-debug/compile_commands.json` creates
  `./compile_commands.json`).
- If LINK_NAME is an existing directory, the link is created inside it, named after TARGET's
  basename.
- With more than two operands, the last must be an existing directory and a link to each
  preceding TARGET is created inside it.

**Options:**

| Option | Description |
|---|---|
| `-s` | Create a symbolic link |
| `-f` | Remove an existing destination first (including a dangling symlink) |
| `-n`, `--no-dereference` | Treat a symlink at the destination as a normal file (replace it rather than follow into it) |
| `-t`, `--target-directory=DIR` | Create all links inside DIR |
| `-v` | Explain what is being done |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
ln -s /usr/bin/python3 python
ln original.txt hardlink.txt
ln -sf newtarget existing-link              # replaces existing-link, even if dangling
ln -sf build/clang-debug/compile_commands.json   # creates ./compile_commands.json
ln -s target.txt some-directory             # creates some-directory/target.txt
ln -s -t bin app1 app2 app3                 # links app1..app3 inside bin/
```

---

## mktemp

Create a temporary file or directory.

**Syntax:**

```
mktemp [OPTIONS]
```

**Description:** Creates a temporary file (or directory with `-d`) with a unique name in the
system temp directory. Prints the path to stdout.

**Options:**

| Option | Description |
|---|---|
| `-d` | Create a directory instead of a file |
| `-p DIR` | Use DIR as the base directory |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
mktemp
# /tmp/tmp.a8Kx3mZq9p
mktemp -d
# /tmp/tmp.b7Lw2nYr8q
mktemp -p /var/tmp
```

---

## head

Output the first lines of files.

**Syntax:**

```
head [OPTIONS] [FILE...]
```

**Description:** Prints the first N lines of each FILE to stdout. If no FILE is given (or FILE
is `-`), reads from standard input. Default is 10 lines.

**Options:**

| Option | Description |
|---|---|
| `-n NUM` | Output the first NUM lines (default: 10) |
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
head file.txt
head -n 5 file.txt
echo "line1\nline2\nline3" | head -n 2
```

---

## tail

Output the last lines of files.

**Syntax:**

```
tail [OPTIONS] [FILE...]
```

**Description:** Prints the last N lines of each FILE to stdout. If no FILE is given (or FILE
is `-`), reads from standard input. Default is 10 lines.

**Options:**

| Option | Description |
|---|---|
| `-n NUM` | Output the last NUM lines (default: 10) |
| `-f` | Follow: output appended data as the file grows |
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
tail file.txt
tail -n 5 file.txt
echo "line1\nline2\nline3" | tail -n 2
tail -f /var/log/syslog
```

---

## wc

Count lines, words, and characters.

**Syntax:**

```
wc [OPTIONS] [FILE...]
```

**Description:** Counts lines, words, and characters in each FILE. If no FILE is given,
reads from standard input. With no options, prints all three counts.

**Options:**

| Option | Description |
|---|---|
| `-l` | Print line count |
| `-w` | Print word count |
| `-c` | Print character count |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
wc file.txt
echo "hello world" | wc -w
# 2
echo "one\ntwo\nthree" | wc -l
# 3
```

---

## sort

Sort lines of text.

**Syntax:**

```
sort [OPTIONS] [FILE...]
```

**Description:** Sorts lines from the given FILEs (or standard input) and writes the result
to standard output.

**Options:**

| Option | Description |
|---|---|
| `-r` | Reverse the sort order |
| `-n` | Compare according to string numerical value |
| `-u` | Output only unique lines |
| `-k FIELD` | Sort by field number (whitespace-delimited) |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
sort names.txt
echo "cherry\napple\nbanana" | sort
# apple
# banana
# cherry
sort -rn scores.txt
sort -k 2 data.txt
```

---

## uniq

Filter adjacent duplicate lines.

**Syntax:**

```
uniq [OPTIONS] [FILE]
```

**Description:** Filters out adjacent duplicate lines from FILE (or standard input).
Input should typically be sorted first for best results.

**Options:**

| Option | Description |
|---|---|
| `-c` | Prefix lines with occurrence count |
| `-d` | Only print duplicate lines |
| `-i` | Ignore case when comparing |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
sort names.txt | uniq
sort names.txt | uniq -c
echo "a\na\nb\na" | uniq
# a
# b
# a
```

---

## cut

Extract fields or characters from lines.

**Syntax:**

```
cut [OPTIONS] [FILE...]
```

**Description:** Extracts selected fields or character ranges from each line of FILE
(or standard input).

**Options:**

| Option | Description |
|---|---|
| `-d DELIM` | Use DELIM as the field delimiter (default: tab) |
| `-f FIELDS` | Select fields (e.g. `1`, `1,3`, `1-3`) |
| `-c CHARS` | Select characters (e.g. `1-5`, `3`) |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
echo "hello:world" | cut -d : -f 1
# hello
echo "abcdef" | cut -c 1-3
# abc
cut -d , -f 2,3 data.csv
```

---

## tr

Translate or delete characters.

**Syntax:**

```
tr [OPTIONS] SET1 [SET2]
```

**Description:** Translates, squeezes, or deletes characters from standard input, writing
to standard output. Character ranges like `a-z` and `A-Z` are expanded.

**Options:**

| Option | Description |
|---|---|
| `-d` | Delete characters in SET1 (no translation) |
| `-s` | Squeeze repeated output characters in SET2 |
| `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
echo "hello world" | tr a-z A-Z
# HELLO WORLD
echo "hello" | tr -d l
# heo
echo "aabbcc" | tr -s a-z
# abc
```

---

## tee

Read from stdin, write to stdout and files.

**Syntax:**

```
tee [OPTIONS] [FILE...]
```

**Description:** Reads from standard input and writes to both standard output and one or
more files simultaneously. Useful for capturing pipeline output while still seeing it.

**Options:**

| Option | Description |
|---|---|
| `-a` | Append to files instead of overwriting |
| `-h`, `--help` | Display help |

**Examples:**

<!-- endo-no-check -->
```endo
echo "hello" | tee output.txt
echo "data" | tee -a log.txt
echo "test" | tee file1.txt file2.txt
echo "quiet" | tee /dev/null     # portable: discards output on Linux, macOS, and Windows
```

**Portability:** The path `/dev/null` is accepted on all platforms. On Windows it
is transparently mapped to the native `NUL` device. See
[Platform Differences → File Paths](platform-differences.md#null-device-portability-devnull-on-windows)
for details.
