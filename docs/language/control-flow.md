# Control Flow

### 9.1 If Expression

`if` is an expression that returns a value. The `else` branch is optional — when
omitted, the false branch returns unit (like F#).

```endo
# Simple if expression
let status = if count > 0 then "has items" else "empty"
let max = if a > b then a else b
let sign = if n < 0 then -1 elif n > 0 then 1 else 0

# Multi-line if expression
let category =
    if age < 13 then
        "child"
    elif age < 20 then
        "teenager"
    elif age < 65 then
        "adult"
    else
        "senior"

# If expression at statement level (for side effects)
if fileExists path then
    echo "File found"
else
    echo "File not found"
    exit 1

# Optional else — if without else returns unit
let mut x = 0
if condition then x <- 42
print x

if verbose then print "debug info"

# Elif chains
if status == 200 then
    echo "OK"
elif status == 404 then
    echo "Not found"
elif status >= 500 then
    echo "Server error"
else
    echo "Unknown: $status"

# Single-line
if test -f $file then cat $file else echo "missing"

# Parentheses optional around condition
if (count > 0) then process
if count > 0 then process           # Same thing
```

### 9.2 Match Expression

See [Pattern Matching](pattern-matching.md) for comprehensive coverage.

```endo
# Match as expression
let description =
    match status with
    | 200 -> "OK"
    | 404 -> "Not Found"
    | code when code >= 500 -> "Server Error"
    | _ -> "Unknown"

# Match as statement
match command with
| "start" -> startServer
| "stop" -> stopServer
| "status" -> showStatus
| cmd -> echo "Unknown command: $cmd"
```

### 9.3 Loops

```endo
# For-in loop over list
for item in [1; 2; 3; 4; 5] do
    echo "Item: $item"
end

# For-in over range
for i in 1..10 do
    echo "Count: $i"
end

for i in 10..-1..1 do
    echo "Countdown: $i"
end

# For-in over command output
for file in $(ls *.txt) do
    echo "Processing: $file"
    wc -l $file
end

# For-in with destructuring
for (name, value) in entries do
    echo "$name = $value"
end

for { host; port } in servers do
    ping $host
end

# While loop
let mut n = 10
while n > 0 do
    echo "Countdown: $n"
    n <- n - 1
end

# Infinite loop with break
while true do
    let input = read
    if input == "quit" then break
    process input
end

# Break and continue
for item in items do
    if item == "skip" then continue
    if item == "stop" then break
    process item
end

# While with complex condition
while hasMoreData && !cancelled do
    processNextBatch
end
```

---
**See also:** [Pattern Matching](pattern-matching.md) | [Error Handling](error-handling.md) | [Command Execution](command-execution.md)
