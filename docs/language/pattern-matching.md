# Pattern Matching

Pattern matching is a powerful way to destructure data and make decisions based on its shape.

### 7.1 Basic Patterns

```endo
# Literal patterns
match x with
| 0 -> "zero"
| 1 -> "one"
| 42 -> "the answer"
| _ -> "something else"

# Variable binding (captures the value)
match x with
| n -> "got $n"

# Wildcard (matches anything, discards)
match x with
| _ -> "don't care what it is"

# String patterns
match command with
| "start" -> startService
| "stop" -> stopService
| "restart" -> restartService
| cmd -> echo "Unknown: $cmd"
```

### 7.2 Compound Patterns

```endo
# Tuple patterns
match point with
| (0, 0) -> "origin"
| (x, 0) -> "on x-axis at $x"
| (0, y) -> "on y-axis at $y"
| (x, y) -> "at ($x, $y)"

# List patterns
match items with
| [] -> "empty list"
| [x] -> "single element: $x"
| [x; y] -> "pair: $x and $y"
| [x; y; z] -> "triple: $x, $y, $z"
| head :: tail -> "head is $head, ${length tail} more elements"
| [first; second; rest...] -> "starts with $first, $second"

# Record patterns
match person with
| { name = "Alice"; _ } -> "Found Alice!"
| { age = 0; _ } -> "Newborn"
| { name; age } -> "$name is $age years old"

# Union/variant patterns
match shape with
| Circle r -> 3.14159 * r * r
| Rectangle (w, h) -> w * h
| Point -> 0.0

# Option patterns
match maybeValue with
| Some x -> "Got: $x"
| None -> "Nothing"

# Result patterns
match result with
| Ok value -> "Success: $value"
| Error { code; message } -> "Error $code: $message"
```

### 7.3 Nested Patterns

```endo
# Deeply nested destructuring
match data with
| { user = { name; role = "admin" }; _ } ->
    "Admin user: $name"
| { user = { name; _ }; active = true } ->
    "Active user: $name"
| { user = { name; _ }; active = false } ->
    "Inactive user: $name"

# List of records
match users with
| [] -> "No users"
| [{ name; _ }] -> "Only user: $name"
| { name = first; _ } :: rest ->
    "First: $first, and ${length rest} others"

# Nested options
match config with
| { database = Some { host; port = Some p }; _ } ->
    connect host p
| { database = Some { host; port = None }; _ } ->
    connect host 5432
| { database = None; _ } ->
    useDefaultDatabase
```

### 7.4 Guards (when clauses)

Guards add conditions to patterns.

```endo
match n with
| x when x < 0 -> "negative"
| x when x == 0 -> "zero"
| x when x > 0 && x < 100 -> "small positive"
| x when x >= 100 -> "large positive"

# Guards with destructuring
match person with
| { age } when age < 0 -> Error "Invalid age"
| { age } when age < 18 -> "Minor"
| { age } when age < 65 -> "Adult"
| { age } -> "Senior"

# Complex guard conditions
match request with
| { method = "GET"; path } when startsWith path "/api" ->
    handleApiGet path
| { method = "GET"; path } ->
    serveStatic path
| { method = "POST"; body = Some b } when length b < 10000 ->
    handlePost b
| { method = "POST"; _ } ->
    Error "Payload too large"
| { method = m; _ } ->
    Error "Method not allowed: $m"

# Guards with captured variables
match items with
| [x; y] when x == y -> "pair of equal elements"
| [x; y] when x > y -> "descending pair"
| [x; y] -> "ascending pair"
| _ -> "not a pair"
```

### 7.5 Or Patterns

Match multiple alternatives with the same result.

```endo
match command with
| "quit" | "exit" | "q" -> exit 0
| "help" | "h" | "?" -> showHelp
| cmd -> execute cmd

match char with
| 'a' | 'e' | 'i' | 'o' | 'u' -> "vowel"
| c when c >= 'a' && c <= 'z' -> "consonant"
| _ -> "not a lowercase letter"

match statusCode with
| 200 | 201 | 204 -> "success"
| 301 | 302 | 307 | 308 -> "redirect"
| 400 | 401 | 403 | 404 -> "client error"
| 500 | 502 | 503 -> "server error"
| code -> "unknown: $code"
```

### 7.6 As Patterns

Bind the entire matched value while also destructuring.

```endo
# Bind whole record while extracting fields
match item with
| { name; price } as product when price > 100 ->
    echo "Expensive product: $product"
    applyDiscount product
| product ->
    echo "Affordable: ${product.name}"
    product

# Useful for recursive structures
match tree with
| Leaf _ as leaf -> [leaf]
| Node (left, right) as node ->
    [node] @ collectNodes left @ collectNodes right
```

---
**See also:** [Type System](type-system.md) | [Control Flow](control-flow.md) | [Error Handling](error-handling.md)
