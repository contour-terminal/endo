# Control Flow

### 9.1 If Expression

`if` is an expression that returns a value. The `else` branch is optional — when
omitted, the false branch returns unit (like F#).

<!-- endo-no-check -->
```endo
# Simple if expression
let status = if count > 0 then "has items" else "empty"
let max = if a > b then a else b
let sign = if n < 0 then -1 else if n > 0 then 1 else 0

# Multi-line if expression
let category =
    if age < 13 then
        "child"
    else if age < 20 then
        "teenager"
    else if age < 65 then
        "adult"
    else
        "senior"

# Multi-expression branches (offside rule)
# Indented lines beyond the `if` column continue the branch.
let rec process (n: int) =
    if n <= 0 then
        let result = 42
        println $"Done: {result}"
        result
    else
        println $"Step {n}"
        process (n - 1)

# If expression at statement level (for side effects)
if fileExists path then
    println "File found"
else
    println "File not found"
    exit 1

# Optional else — if without else returns unit
let mut x = 0
if condition then x <- 42
print x

if verbose then print "debug info"

# Chained conditions
if status == 200 then
    println "OK"
else if status == 404 then
    println "Not found"
else if status >= 500 then
    println "Server error"
else
    println $"Unknown: {status}"

# Single-line
if test -f $file then cat $file else echo "missing"

# Parentheses optional around condition
if (count > 0) then process
if count > 0 then process           # Same thing
```

### 9.2 Match Expression

See [Pattern Matching](pattern-matching.md) for comprehensive coverage.

<!-- endo-no-check -->
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
| cmd -> println $"Unknown command: {cmd}"
```

### 9.3 Loops

Loop bodies use the **offside rule** — statements indented past the `for`/`while`
keyword belong to the body. No closing keyword is needed.

<!-- endo-no-check -->
```endo
# For-in loop over list
for item in [1; 2; 3; 4; 5] do
    println $"Item: {item}"

# For-in over range
for i in 1..10 do
    println $"Count: {i}"

for i in 10..-1..1 do
    println $"Countdown: {i}"

# For-in over command output
for file in $(ls *.txt) do
    println $"Processing: {file}"
    wc -l $file

# For-in with destructuring
for (name, value) in entries do
    println $"{name} = {value}"

for { host; port } in servers do
    ping $host

# While loop
let mut n = 10
while n > 0 do
    println $"Countdown: {n}"
    n <- n - 1

# Infinite loop with break
while true do
    let input = read
    if input == "quit" then break
    process input

# Break and continue
for item in items do
    if item == "skip" then continue
    if item == "stop" then break
    process item

# While with complex condition
while hasMoreData && !cancelled do
    processNextBatch

# Single-line loops
for x in [1; 2; 3] do print x
while count > 0 do count <- count - 1
```

---
**See also:** [Pattern Matching](pattern-matching.md) | [Error Handling](error-handling.md) | [Command Execution](command-execution.md)
