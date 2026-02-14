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
```

**Description:** Changes the shell's working directory. With no argument, changes to `$HOME`.
Supports tilde expansion (`~`, `~user`).

**Example:**

```endo
cd /tmp
cd ~
cd ~/projects/endo
```

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
while true; do
    echo "loop"
    break
done
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
if false; then
    echo "unreachable"
fi
```

---

## which

Locate a command.

**Syntax:**

```
which command
```

**Description:** Searches `$PATH` for the given command and prints its full path. Returns
a non-zero exit code if the command is not found.

**Example:**

```endo
which git
# /usr/bin/git

which nonexistent
# (returns exit code 1)
```

---

## cat

Concatenate and display files.

**Syntax:**

```
cat [file...]
```

**Description:** Reads files sequentially and writes their contents to standard output. If
no files are given, reads from standard input.

**Example:**

```endo
cat README.md
cat file1.txt file2.txt > combined.txt
echo "hello" | cat
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
bind KEY ACTION         # Bind a key to an action
bind -r KEY             # Remove a binding
bind --reset            # Reset to default bindings
bind --help             # Show available actions and key format
```

**Description:** Manages the shell's key bindings at runtime. Changes take effect
immediately.

**Example:**

```endo
# List all bindings
bind

# Bind Ctrl+Y to yank (paste from kill ring)
bind ctrl+y yank

# Remove a binding
bind -r ctrl+y

# Show help
bind --help
```

See [Configuration](configuration.md#key-bindings) for details on key format and available
actions.

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
