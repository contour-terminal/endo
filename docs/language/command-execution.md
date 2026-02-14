# Command Execution

### 10.1 Statement Context (Output to Terminal)

When a command appears as a statement (not in an expression), its output goes directly to the terminal.

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
if needsUpdate then
    echo "Updating..."
    git pull
    make build
fi
```

### 10.2 Expression Context (Capture Output)

When a command is part of an expression (assignment, function argument, etc.), its stdout is captured.

```endo
# Assignment captures stdout
let files = ls -la
let count = wc -l < README.md
let user = whoami
let today = date +%Y-%m-%d

# Trailing newline is trimmed
let name = echo "test"            # "test" not "test\n"

# Capture in expressions
let greeting = "Hello, $(whoami)!"
let info = "Files: ${ls | wc -l}"

# Captured output as list
let fileList = ls | lines
let nonEmpty = cat file.txt | lines |> filter (fun l -> l != "")

# In conditionals
if $(grep -q pattern file.txt) then
    echo "Pattern found"
fi

# As function arguments
process (cat config.txt)
analyze (git diff HEAD~1)
```

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

```endo
# Compare output of two commands
diff <(ls dir1) <(ls dir2)
diff <(sort file1) <(sort file2)

# Use command output as input file
while read line do
    process $line
done < <(find . -name "*.txt")

# Multiple process substitutions
paste <(cut -f1 file1) <(cut -f2 file2)

# Output process substitution
tee >(gzip > backup.gz) < input.txt

# Complex example
comm -12 <(sort users_today | uniq) <(sort users_yesterday | uniq)
```

### 10.6 Command Substitution

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

---
**See also:** [Operators & Pipelines](operators-and-pipelines.md) | [Error Handling](error-handling.md) | [Interoperability](interoperability.md)
