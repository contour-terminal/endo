# Interoperability: F# Style vs Bash Style

### 13.1 When to Use Each Style

| Task | Recommended Style | Example |
|------|-------------------|---------|
| Data transformation | F# style | `data \|> map transform \|> filter valid` |
| Process pipelines | Bash style | `ps aux \| grep nginx \| wc -l` |
| Configuration | F# records | `{ host = "localhost"; port = 8080 }` |
| Quick scripts | Bash style | `for f in *.txt do cat $f end` |
| Complex logic | F# match | `match result with \| Ok x -> ... \| Error e -> ...` |
| Conditionals | F# style | `if complex_expr then branch1 else branch2` |
| Error handling | F# style | `let result = operation?; match result with ...` |
| One-off commands | Bash style | `ls -la; git status` |
| Reusable functions | F# style | `let process x = x \|> transform \|> validate` |

### 13.2 Mixing Styles

```endo
# Start with shell command, process with functions
ls -la
|> lines
|> filter (fun l -> !startsWith l ".")
|> map (fun l -> l |> words |> last)
|> sort
|> each echo

# Function that wraps shell commands
let backup dir =
    let timestamp = date +%Y%m%d_%H%M%S
    let backupDir = "${dir}_backup_$timestamp"
    cp -r $dir $backupDir
    gzip -r $backupDir
    echo "Backed up to $backupDir"
    Ok backupDir

# Shell commands using function results
let files = findLargeFiles "/var/log" 100
for file in $files do
    echo "Compressing: $file"
    gzip $file
end

# Conditional mixing F# and shell commands
if fileExists config then
    let cfg = loadConfig config
    match cfg.mode with
    | "development" ->
        echo "Starting dev server..."
        npm run dev
    | "production" ->
        echo "Starting production..."
        npm run build && npm start
    | m ->
        echo "Unknown mode: $m"
        exit 1
else
    echo "No config found, using defaults"
    useDefaultConfig

# Complex data pipeline with shell tools
let topContributors =
    git log --format="%an"
    | lines
    |> groupBy id
    |> map (fun (name, commits) -> { name = name; count = length commits })
    |> sortByDescending (fun c -> c.count)
    |> take 10
    |> each (fun c -> echo "${c.name}: ${c.count} commits")
```

### 13.3 Automatic Type Coercion

Endo automatically converts between types where sensible.

```endo
# Command output -> String
let content = cat file.txt        # content: str

# Command output -> Lines (with | lines)
let lineList = cat file.txt | lines   # lineList: list<str>

# String -> Command argument
let pattern = "*.txt"
ls $pattern                       # Pattern is expanded by shell

# List -> Command arguments
let flags = ["-l"; "-a"; "-h"]
ls $flags                         # Equivalent to: ls -l -a -h

# Record -> JSON (for APIs)
let config = { name = "test"; value = 42 }
curl -d ${toJson config} https://api.example.com

# Numbers in strings
let count = 42
echo "Count: $count"              # int -> str automatically

# String to number (explicit)
let n = parseInt "42"             # str -> int
let f = parseFloat "3.14"         # str -> float
```

### 13.4 Dual-Mode Builtins

Some builtins adapt their behavior based on how they are called. When invoked with bare
arguments they behave like traditional shell commands; when invoked with quoted or
parenthesized arguments they return typed F# values.

| Builtin | Shell form | F# form | F# return type |
|---------|-----------|---------|----------------|
| `env` | `echo $VAR` | `env "VAR"` | `Option<string>` |
| `which` | `which git` | `which "git"` | `Option<string>` |

```endo
# Shell mode: prints path, sets exit code
which git
# /usr/bin/git

# F# mode: returns Option<string> for pattern matching
match which "git" with
| Some path -> println $"Found: {path}"
| None -> println "not installed"
```

### 13.5 Function and Command Resolution

When you call something, endo resolves it in this order:

1. **User-defined functions** (current scope)
2. **Imported functions** (from modules)
3. **Built-in functions** (map, filter, etc.)
4. **Shell builtins** (cd, export, etc.)
5. **External commands** (PATH lookup)

```endo
# If you define 'echo', it shadows the builtin
let echo msg =
    builtin echo "[LOG] $msg"

# Force specific resolution
let result = builtin echo "using builtin"
let result = command /bin/echo "using external"
let result = myModule.echo "using imported"

# Check resolution
which echo                        # Shows what 'echo' resolves to
type echo                         # Shows type (function/builtin/external)

# List all
let allFunctions = functions      # User-defined functions
let allBuiltins = builtins        # Shell builtins
```

### 13.6 Transitioning from Bash

Common patterns and their endo equivalents:

```endo
# Bash: VAR="value"
# Endo:
let var = "value"

# Bash: export VAR="value"
# Endo:
export VAR = "value"

# Bash: if [ -f "$file" ]; then cat "$file"; fi
# Endo:
if fileExists file then cat $file

# Bash: for f in *.txt; do echo "$f"; done
# Endo:
for f in $(ls *.txt) do echo $f end
# or
ls *.txt | lines |> each echo

# Bash: result=$(command)
# Endo:
let result = command

# Bash: command1 && command2 || command3
# Endo:
if command1 then command2 else command3

# Bash: arr=(1 2 3); echo ${arr[0]}
# Endo:
let arr = [1; 2; 3]; echo ${nth 0 arr}

# Bash: ${var:-default}
# Endo (same):
${var:-default}
# or F# style:
var ?| "default"

# Bash: function name() { ... }
# Endo:
let name args = ...

# Bash: case $x in pattern) cmd;; esac
# Endo:
match x with | pattern -> cmd | _ -> ()
```

---
**See also:** [Command Execution](command-execution.md) | [Operators & Pipelines](operators-and-pipelines.md) | [Functions](functions.md) | [Error Handling](error-handling.md)
