# Command Execution

### 10.1 Statement Context (Output to Terminal)

When a command appears as a statement (not in an expression), its output goes directly to the terminal.

<!-- endo-no-check -->
```endo
# Commands print to stdout/stderr
ls -la
echo "Hello, World"
find . -name "*.txt"

# Side effects execute
rm -f temp.txt
mkdir -p new_directory
git commit -m "Update"

# Pipeline statements
ps aux | grep nginx
cat file.txt | sort | uniq

# In blocks
if needsUpdate then {
    echo "Updating..."
    git pull
    make build
}
```

### 10.2 Expression Context (Capture Output)

When a command is part of an expression (assignment, function argument, etc.), its stdout is captured.

<!-- endo-no-check -->
```endo
# Assignment captures stdout
let files = ls -la
let count = wc -l < README.md
let user = whoami
let today = date +%Y-%m-%d

# Trailing newline is trimmed
let name = echo "test"            # "test" not "test\n"

# Capture in expressions
let greeting = $"Hello, {whoami}!"
let info = $"Files: {ls | wc -l}"

# Captured output as list
let fileList = ls | lines
let nonEmpty = cat file.txt | lines |> filter (fun l -> l != "")

# In conditionals
if $(grep -q pattern file.txt) then
    echo "Pattern found"

# As function arguments
process (cat config.txt)
analyze (git diff HEAD~1)
```

### Quoting the Program Path

The program name in command position is normally a bare word. If the path contains
characters that would otherwise split it into several tokens — spaces, `!`, `$`, or a
leading `//` (a UNC path, whose unquoted `//` starts a C-style comment) — wrap it in
quotes. This is most common on Windows with drive-letter and UNC paths:

<!-- endo-no-check -->
```endo
# Drive-letter path with a space and a '!'
& "X:/Program Files/!Tools/tool.exe" --help

# UNC path — must be quoted, since an unquoted // starts a comment
& "//server/share/tools/tool.exe" --help

# Double quotes interpolate exactly like a quoted argument. `$VAR` reads an
# environment variable, so export it (or use one already in the environment):
let export dir = "X:/Program Files/!Tools"
& "$dir/tool.exe" --help

# Single quotes are literal (no interpolation):
& 'X:/Program Files/!Tools/tool.exe' --help
```

Arguments after the program are quoted the same way. For a program path that is only
known at runtime from an F# expression (a variable, `which`, or a pattern match), use
[`exec`](#107-dynamic-command-execution-exec) instead.

> **Tip:** interactive TAB-completion inserts these quotes for you — completing a path
> that contains a space or other special character produces an already-quoted token.

### 10.3 String Interpolation

```endo
# Variable interpolation
let name = "World"
echo "Hello, $name"               # Hello, World
echo "Path: ${HOME}/docs"         # Path: /home/user/docs

# Expression interpolation
echo "Sum: $((1 + 2 * 3))"        # Sum: 7
echo "Files: $(ls | wc -l)"       # Files: 42
echo "Upper: ${name |> toUpper}"  # Upper: WORLD

# Nested interpolation
let user = "alice"
echo "Home: ${getenv "HOME_$user"}"

# Escape to prevent interpolation
echo "Literal \$name"             # Literal $name
echo "Price: \$99.99"             # Price: $99.99

# Single quotes: no interpolation
echo 'No $interpolation here'     # No $interpolation here
echo 'Path: $HOME'                # Path: $HOME
```

### 10.4 Redirections

<!-- endo-no-check -->
```endo
# Output redirection
echo "log entry" > logfile.txt    # Overwrite
echo "more" >> logfile.txt        # Append

# Input redirection
sort < unsorted.txt
wc -l < README.md

# Stderr redirection
command 2> errors.txt             # Stderr to file
command 2>> errors.txt            # Append stderr
command 2>&1                      # Stderr to stdout

# Combined redirects
command > output.txt 2>&1         # Both to file
command &> all.txt                # Shorthand for above
command 2>&1 | tee log.txt        # Both to pipe

# Discard output
command > /dev/null               # Discard stdout
command 2> /dev/null              # Discard stderr
command &> /dev/null              # Discard both

# Here documents
cat <<EOF
This is a multi-line
here document with $name interpolation
and $(command) substitution
EOF

# Here document without interpolation
cat <<'EOF'
No interpolation here
$VAR stays as literal $VAR
$(cmd) stays as literal
EOF

# Here strings
cat <<< "Single line input"
grep pattern <<< $variable
wc -w <<< "count these words"
```

### 10.5 Process Substitution

Treat command output as a file.

<!-- endo-no-check -->
```endo
# Compare output of two commands
diff <(ls dir1) <(ls dir2)
diff <(sort file1) <(sort file2)

# Use command output as input file
while read line do
    process $line
end < <(find . -name "*.txt")

# Multiple process substitutions
paste <(cut -f1 file1) <(cut -f2 file2)

# Output process substitution
tee >(gzip > backup.gz) < input.txt

# Complex example
comm -12 <(sort users_today | uniq) <(sort users_yesterday | uniq)
```

!!! note "Windows"
    Process substitution is not yet available on Windows. On Linux and macOS, it is
    implemented using `/dev/fd` paths. A future release may add Windows support via named
    pipes. See [Platform Differences](../shell/platform-differences.md#process-substitution)
    for details.

### 10.6 Command Substitution

<!-- endo-no-check -->
```endo
# $() syntax (preferred)
let user = $(whoami)
let files = $(ls *.txt)
echo "Today is $(date +%A)"

# Backtick syntax (legacy, supported)
let user = `whoami`
echo "Today is `date +%A`"

# Nested substitution (only works with $())
let result = $(cat $(find . -name "config.txt" | head -1))

# Arithmetic substitution
let sum = $((1 + 2 * 3))
let next = $((counter + 1))
echo "Result: $((a * b + c))"
```

#### `$(...)` in F# Expressions

`$(...)` also works as a self-delimiting F# expression that captures stdout as a
string. Unlike `& command` which is greedy (consumes up to a statement boundary),
`$(...)` composes naturally inline:

<!-- endo-no-check -->
```endo
# Self-delimiting — composes with operators
let greeting = $(whoami) + "@" + $(hostname)

# In if-conditions
if $(git status --porcelain) == "" then print "clean" else print "dirty"

# As pipeline source
$(echo 42) |> string_length |> print   # prints 2

# Compare with & command (needs parentheses for inline use)
let greeting = (& whoami) + "@" + (& hostname)
```

### 10.7 Dynamic Command Execution (`exec`)

The `exec` keyword executes a dynamically-resolved program path with F# expression arguments.
Unlike shell commands where the program name is a compile-time literal, `exec` takes runtime string
values — enabling conditional command dispatch via `which` and pattern matching.

```endo
# Single command with literal path
exec "/usr/bin/fortune"

# With arguments
exec "/usr/bin/fortune" "-s"

# Pipeline via | — true OS-level streaming pipes
exec "/usr/bin/fortune" | exec "/usr/bin/lolcat"

# Three-stage pipeline
exec "/bin/echo" "hello" | exec "/usr/bin/tr" "a-z" "A-Z" | exec "/bin/cat"

# Variable program paths (the main use case)
let f = "/usr/bin/fortune"
let l = "/usr/bin/lolcat"
exec f | exec l

# F# expression arguments
let flags = "-s"
exec f flags

# The motivating use case: which + match + exec
match which "fortune", which "lolcat" with
| Some f, Some l -> (exec f | exec l)
| Some f, None   -> exec f
| _              -> println "Commands not found"
```

**Key differences from shell `|` pipes:**
- Program paths and arguments are F# expressions (variables, function calls, string literals)
- True OS-level pipe semantics (stdout→stdin streaming)
- Inside `match` arms, parentheses are needed: `(exec f | exec l)` to disambiguate `|` from arm separators
- Returns the exit code of the last command in the pipeline

### 10.6 Multi-line Commands

Long shell commands can span multiple lines. Endo recognises three forms of
line continuation. Bash-style `\` at end of line is **not** supported.

**Trailing operator** — if a line ends in `|`, `|>`, `&&`, or `||`, the command
continues on the next line (indentation is irrelevant):

<!-- endo-no-check -->
```endo
# Trailing `|`
ls -la |
    grep ".endo" |
    wc -l

# Trailing `&&` / `||`
make build &&
    make test

# Trailing `|>` (F# pipeline)
readFile "data.json" |>
    parseJson |>
    validate
```

**Leading operator** — if the next line starts with `|`, `|>`, `&&`, or `||`
(at any column), it continues the previous command. This is idiomatic F#:

<!-- endo-no-check -->
```endo
[1; 2; 3]
    |> List.map (fun x -> x * 2)
    |> List.filter (fun x -> x > 2)
    |> List.sum

ls -la
    | grep ".endo"
    | wc -l

testsPassed
    && publishArtifact
    || notifyFailure
```

**Indentation** — if the next line's first token sits at a column strictly
greater than the program name's column, it is parsed as a continuation of the
command above, regardless of operators:

<!-- endo-no-check -->
```endo
git commit -m "blurb"
           --author "John Doe"
           --date "2024-01-01"
```

Here `git` is at column 1; the `--author` and `--date` lines are indented past
column 1, so they extend the `git commit` invocation.

The three forms can be combined freely. A command at column 1 followed by a
line at column 1 starts a new, independent command.

---
**See also:** [Operators & Pipelines](operators-and-pipelines.md) | [Error Handling](error-handling.md) | [Interoperability](interoperability.md)
