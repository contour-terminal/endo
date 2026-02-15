# Error Handling

### 11.1 Result Type

Commands in expression context return a `result` type for explicit error handling.

<!-- endo-no-check -->
```endo
# Commands return Result<str, Error>
let result = cat nonexistent.txt

match result with
| Ok content -> println $"Content: {content}"
| Error e -> println $"Error ({e.code}): {e.message}"

# Built-in Error type
type Error = {
    code: int        # Exit code
    message: str     # Error description
}

# Result type definition
type result<T, E> =
    | Ok of T
    | Error of E

# Checking results
let result = fetchData url
if isOk result then
    process (unwrap result)
else
    log $"Failed: {getError result}"
```

### 11.2 Error Propagation with `?`

The `?` operator propagates errors up the call stack (similar to Rust).

<!-- endo-no-check -->
```endo
# Propagate errors automatically
let processFile path =
    let content = cat $path?           # Returns Error if cat fails
    let parsed = parseJson content?    # Returns Error if parse fails
    transform parsed                   # Returns Ok result

# Equivalent explicit version
let processFile path =
    match cat $path with
    | Error e -> Error e
    | Ok content ->
        match parseJson content with
        | Error e -> Error e
        | Ok parsed -> Ok (transform parsed)

# Chain multiple fallible operations
let deployApp =
    buildProject?
    runTests?
    packageArtifacts?
    uploadToServer?
    notifyTeam

# Use in pipelines
let data =
    fetchUrl url?
    |> parseJson?
    |> extractField "data"?
    |> validate?
```

### 11.3 Try-With Expression

Catch and handle errors explicitly.

<!-- endo-no-check -->
```endo
# Basic try-with
let result = try
    let data = fetchData url?
    let parsed = parseJson data?
    processData parsed
with
| { code = 404; _ } ->
    DefaultData.empty
| { code = c; message = m } when c >= 500 ->
    log $"Server error: {m}"
    Error { code = c; message = m }
| e ->
    Error e

# Try with specific error patterns
let config = try
    loadConfig configPath?
with
| { message = m } when contains m "not found" ->
    println "Config not found, using defaults"
    DefaultConfig
| { message = m } when contains m "permission" ->
    println $"Cannot read config: {m}"
    exit 1
| e ->
    println $"Unexpected error: {e.message}"
    exit 1

# Try-finally for cleanup
let result = try
    let handle = openFile path
    processFile handle
finally
    closeFile handle
```

### 11.4 Option Type for Missing Values

Use `option` for values that might not exist.

<!-- endo-no-check -->
```endo
# Option type
type option<T> = Some of T | None

# Functions returning Option
let find predicate list =
    match filter predicate list with
    | [] -> None
    | [x; _...] -> Some x

let lookup key map =
    match filter (fun (k, _) -> k == key) map with
    | [(_, v)] -> Some v
    | _ -> None

# Using options
match findUser id with
| Some user -> greet user.name
| None -> echo "User not found"

# Option combinators — module-qualified syntax
let email =
    findUser id
    |> Option.map (fun u -> u.email)
    |> Option.defaultValue "no-email@example.com"

let result =
    findUser id
    |> Option.bind (fun u -> u.manager)     # Returns option<User>
    |> Option.map (fun m -> m.email)

# Option combinators — method-style syntax
let email = (findUser id).map (fun u -> u.email)
let name = (findUser id).defaultValue "anonymous"
let manager = (findUser id).bind (fun u -> u.manager)

# Optional chaining (syntactic sugar)
let email = user?.profile?.email ?| "default@example.com"

# The above is equivalent to:
let email =
    match user with
    | None -> "default@example.com"
    | Some u ->
        match u.profile with
        | None -> "default@example.com"
        | Some p ->
            match p.email with
            | None -> "default@example.com"
            | Some e -> e
```

### 11.5 Exit Codes (Bash Compatibility)

Traditional exit code handling is fully supported.

<!-- endo-no-check -->
```endo
# Access last exit code
someCommand
if $? == 0 then
    echo "Success"
else
    echo "Failed with code $?"

# Exit with code
exit 0                            # Success
exit 1                            # General error
exit 127                          # Command not found

# Return from function with value
let checkFile path =
    if test -f $path then return 0 else return 1

# Check command success in conditions
if grep -q pattern file.txt then echo "Found"

# Boolean commands
if test -f $file && test -r $file then cat $file
```

---
**See also:** [Pattern Matching](pattern-matching.md) | [Control Flow](control-flow.md) | [Command Execution](command-execution.md)
