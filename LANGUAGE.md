# Endo Language Specification

**Version:** 0.1.0 (Draft)

Endo is a modern shell language that combines F#-inspired functional programming with bash-compatible shell scripting. It provides type inference, pattern matching, and functional pipelines while maintaining the interactive convenience of traditional shells.

---

## Table of Contents

1. [Philosophy & Goals](#1-philosophy--goals)
2. [Lexical Elements](#2-lexical-elements)
3. [Type System](#3-type-system)
4. [Variables & Bindings](#4-variables--bindings)
5. [Functions](#5-functions)
6. [Lists & Collections](#6-lists--collections)
7. [Pattern Matching](#7-pattern-matching)
8. [Operators & Pipelines](#8-operators--pipelines)
9. [Control Flow](#9-control-flow)
10. [Command Execution](#10-command-execution)
11. [Error Handling](#11-error-handling)
12. [Modules and Imports](#12-modules-and-imports)
13. [Interoperability: F# Style vs Bash Style](#13-interoperability-f-style-vs-bash-style)
14. [Implementation Notes for Parser](#14-implementation-notes-for-parser)
15. [EBNF Grammar](#15-ebnf-grammar)

---

## 1. Philosophy & Goals

### Core Principles

- **F#-inspired functional shell** - Bring functional programming ergonomics to shell scripting
- **Bash convenience** - Familiar syntax for common operations, easy transition for existing shell users
- **Type inference by default** - Types are automatically deduced; annotations are optional for documentation or disambiguation
- **Unified command model** - Functions and external commands share invocation syntax
- **Expression-oriented** - Most constructs return values and can be composed

### Non-Goals

- Full POSIX compliance (we prioritize compatibility over strict compliance)
- Complete F# feature parity (practical shell focus takes precedence)
- Replacing bash entirely (interoperability is key)

### Design Rationale

Endo recognizes that shell scripting and functional programming serve complementary needs:

| Shell Strengths | Functional Strengths |
|-----------------|---------------------|
| Process orchestration | Data transformation |
| System interaction | Complex logic |
| Quick one-liners | Type safety |
| Tool composition | Refactoring confidence |

Endo combines both, letting you choose the right style for each task.

---

## 2. Lexical Elements

### 2.1 Keywords

```
let       mut       fun       type      match     with
when      if        then      else      elif      fi
for       in        do        done      while     try
return    break     continue  export    true      false
Ok        Error     Some      None      rec       and
of        as        global
```

### 2.2 Reserved Operators

```
|>        |         ->        <-        =>        ::
&&        ||        ==        !=        <=        >=
<         >         +         -         *         /        %
**        >>        <<        @         #         ?        !
```

### 2.3 Delimiters

```
( )       [ ]       { }       ;         ,         .
```

### 2.4 Comments

```fsharp
# Single line comment (shell style)
// Single line comment (C style)
(* 
   Multi-line comment 
   F# style 
*)
```

### 2.5 String Literals

```fsharp
# Double-quoted strings (with interpolation)
"Hello, $name"
"Value: ${expression}"
"Command output: $(whoami)"
"Arithmetic: $((1 + 2))"

# Single-quoted strings (literal, no interpolation)
'No $interpolation here'
'Literal backslash: \'

# Escape sequences in double-quoted strings
"\n"      # Newline
"\t"      # Tab
"\\"      # Backslash
"\$"      # Literal dollar sign
"\""      # Literal double quote

# F#-style interpolated strings (expression holes with {expr})
$"Hello, {name}"
$"Sum is {3 + 4}"
$"a={a}, b={b}"
$"result: {f 5}"
$"val: {if x > 0 then "positive" else "negative"}"

# Escaped braces in F#-style interpolated strings
$"{{literal braces}}"   # produces: {literal braces}
```

### 2.6 Numeric Literals

```fsharp
# Integers
42
-17
0xFF        # Hexadecimal
0o755       # Octal
0b1010      # Binary

# Floating point
3.14
-0.5
1e10
2.5e-3
```

---

## 3. Type System

Endo uses type inference to automatically deduce types. You can optionally add type annotations for documentation, disambiguation, or to catch errors earlier.

### 3.1 Primitive Types

| Type | Description | Examples |
|------|-------------|----------|
| `int` | 64-bit signed integer | `42`, `-17`, `0xFF` |
| `float` | 64-bit floating point | `3.14`, `-0.5`, `1e10` |
| `str` | UTF-8 string | `"hello"`, `'literal'` |
| `bool` | Boolean | `true`, `false` |
| `unit` | No value (like void) | `()` |

### 3.2 Compound Types

```fsharp
# Lists (homogeneous, variable length)
list<int>           # [1; 2; 3]
list<str>           # ["a"; "b"; "c"]

# Tuples (heterogeneous, fixed size)
(int, str)          # (42, "hello")
(int, str, bool)    # (1, "x", true)

# Option (represents presence or absence of a value)
option<int>         # Some 42 or None
option<str>         # Some "value" or None

# Result (represents success or failure)
result<int, str>    # Ok 42 or Error "failed"
result<str, Error>  # Ok "data" or Error { code = 1; message = "..." }
```

### 3.3 Type Inference Examples

```fsharp
# Types are inferred automatically
let x = 42                    # x: int
let name = "Alice"            # name: str
let items = [1; 2; 3]         # items: list<int>
let pair = (1, "hello")       # pair: (int, str)
let double = fun x -> x * 2   # double: int -> int

# Inference through usage
let add x y = x + y           # add: int -> int -> int (inferred from +)
let greet name = "Hi, $name"  # greet: str -> str

# Explicit annotations when needed
let count: int = 42
let ratio: float = 42         # Would be int without annotation
let empty: list<str> = []     # Empty list needs type hint

# Function annotations
let add (x: int) (y: int): int = x + y
let parse (s: str): result<int, str> = tryParseInt s
```

### 3.4 Records

Records are named collections of fields. They provide structured data with named access.

```fsharp
# Define a record type
type Person = {
    name: str
    age: int
    email: option<str>
}

# Create record instances
let alice = { name = "Alice"; age = 30; email = Some "alice@example.com" }
let bob = { name = "Bob"; age = 25; email = None }

# Access fields
let aliceName = alice.name            # "Alice"
let bobAge = bob.age                  # 25

# Copy with update (functional update)
let olderAlice = { alice with age = 31 }
let bobWithEmail = { bob with email = Some "bob@example.com" }

# Nested records
type Address = { 
    city: str
    country: str 
}

type Employee = { 
    person: Person
    address: Address
    salary: int 
}

let emp = {
    person = alice
    address = { city = "NYC"; country = "USA" }
    salary = 100000
}

# Nested access
let city = emp.address.city           # "NYC"

# Nested update
let relocated = { emp with address = { emp.address with city = "Boston" } }
```

### 3.5 Discriminated Unions (Algebraic Data Types)

Unions represent values that can be one of several named cases, optionally with associated data.

```fsharp
# Define a union type
type Shape =
    | Circle of radius: float
    | Rectangle of width: float * height: float
    | Point

# Create instances
let c = Circle 5.0
let r = Rectangle (10.0, 20.0)
let p = Point

# Pattern match on unions
let area shape =
    match shape with
    | Circle r -> 3.14159 * r * r
    | Rectangle (w, h) -> w * h
    | Point -> 0.0

# More complex union
type JsonValue =
    | JsonNull
    | JsonBool of bool
    | JsonNumber of float
    | JsonString of str
    | JsonArray of list<JsonValue>
    | JsonObject of list<(str, JsonValue)>

# Built-in unions (defined by the language)
# type option<T> = Some of T | None
# type result<T, E> = Ok of T | Error of E
```

---

## 4. Variables & Bindings

### 4.1 Immutable Bindings

By default, `let` creates immutable bindings that cannot be reassigned.

```fsharp
# Basic immutable binding
let x = 42
let message = "Hello, World"
let items = [1; 2; 3; 4; 5]

# Attempting to reassign is an error
let x = 10
x = 20                        # ERROR: Cannot reassign immutable binding 'x'

# But you can shadow with a new binding
let x = 10
let x = x + 1                 # OK: Creates new binding, x is now 11

# Shadowing is useful for transformations
let input = "  hello  "
let input = trim input        # Shadow with trimmed version
let input = toUpper input     # Shadow with uppercase version
```

### 4.2 Mutable Bindings

Use `let mut` when you need to modify a value.

```fsharp
# Mutable binding
let mut counter = 0
let mut name = "initial"

# Reassign with <- operator
counter <- counter + 1
counter <- counter + 1
echo "Counter: $counter"      # Counter: 2

name <- "updated"

# Mutable variables in loops
let mut sum = 0
for n in [1; 2; 3; 4; 5] do
    sum <- sum + n
done
echo "Sum: $sum"              # Sum: 15

# Mutable is required for accumulation patterns
let mut result = []
for line in $(cat file.txt) | lines do
    if startsWith line "#" then
        result <- result @ [line]
    fi
done
```

### 4.3 Export Bindings

Use `let export` to bind a value and simultaneously export it as an environment variable.
The expression is evaluated, bound as a normal F# variable, and its string representation
is exported to the environment.

```fsharp
# Export a number — binds X = 42 and exports X="42"
let export X = 42

# Export a computed value
let export PATH_COUNT = length (split PATH ":")

# Export with mutable binding
let export mut LEVEL = 1
LEVEL <- LEVEL + 1            # Note: mutation does not re-export

# String and boolean exports
let export GREETING = "hello"
let export VERBOSE = true     # exports as "true"
```

> **Note:** `let export rec` is not allowed — functions cannot be exported.
> Export happens at binding time only; subsequent mutations are not re-exported.

### 4.4 Destructuring

Extract values from compound types directly in bindings.

```fsharp
# Tuple destructuring
let (x, y) = (10, 20)
let (first, second, third) = ("a", "b", "c")
let (a, _) = (1, 2)           # Ignore second element with _
let (_, _, z) = (1, 2, 3)     # Only care about third

# Record destructuring
let { name; age } = person
let { name = n; age = a } = person    # Rename bindings
let { name; _ } = person              # Ignore other fields with _

# List destructuring
let [a; b; c] = [1; 2; 3]             # Exact match
let [head; rest...] = [1; 2; 3; 4]    # head=1, rest=[2;3;4]
let [first; second; _...] = items     # Ignore tail

# Nested destructuring
let { person = { name; age }; salary } = employee

# In function parameters
let greet { name; _ } = "Hello, $name"
let addPair (a, b) = a + b
let sumFirst [x; y; _...] = x + y
```

### 4.5 Scope and Visibility

```fsharp
# Block scope with braces
let outer = 10
let result = {
    let inner = 20            # Only visible in this block
    inner + outer
}
# 'inner' is not visible here
echo "Result: $result"        # Result: 30

# Block scope with indentation (in functions)
let process x =
    let temp = x * 2          # Local to function
    let helper y = y + 1      # Nested function
    helper temp

# Export for child processes
export PATH
export MY_VAR = "value"
let MY_OTHER = "local"
export MY_OTHER               # Export existing variable

# Global modifier (escape local scope)
let processConfig =
    let global CONFIG_CACHE = loadConfig    # Visible outside function
    CONFIG_CACHE
```

---

## 5. Functions

### 5.1 Named Functions (Curried)

Functions in endo are curried by default, meaning multi-parameter functions are actually chains of single-parameter functions.

```fsharp
# Simple single-parameter function
let double x = x * 2
let greet name = echo "Hello, $name"

# Multi-parameter functions (curried)
let add x y = x + y
let multiply x y z = x * y * z

# Call with all arguments
let sum = add 3 5             # 8
let product = multiply 2 3 4  # 24

# With type annotations
let add (x: int) (y: int): int = x + y
let format (template: str) (value: int): str = "$template: $value"

# Partial application (supply fewer arguments)
let add5 = add 5              # add5: int -> int
let result = add5 10          # 15

let greetFormal = format "Dear"
let msg = greetFormal 42      # "Dear: 42"

# Partial application is powerful for pipelines
let multiplyBy n = multiply n
let times10 = multiplyBy 10

[1; 2; 3] |> map (add 1)      # [2; 3; 4]
[1; 2; 3] |> map times10      # [10; 20; 30]
```

### 5.2 Multi-line Functions

```fsharp
# Indentation-based body
let factorial n =
    match n with
    | 0 -> 1
    | 1 -> 1
    | n -> n * factorial (n - 1)

# Brace-based body
let fibonacci n = {
    let mut a = 0
    let mut b = 1
    for _ in 1..n do
        let temp = a
        a <- b
        b <- temp + b
    done
    a
}

# Mixed shell and functional
let findLargeFiles dir minSize =
    find $dir -size +${minSize}M
    |> lines
    |> filter (fun f -> test -f $f)
    |> map (fun f -> { path = f; size = stat -c%s $f })

# Multiple statements in function body
let processAndLog input =
    let processed = transform input
    log "Processed: $processed"
    let validated = validate processed
    log "Validated: $validated"
    validated
```

### 5.3 Lambda Expressions

Anonymous functions for inline use.

```fsharp
# Basic lambda syntax: fun params -> body
let double = fun x -> x * 2
let add = fun x y -> x + y

# Lambdas in higher-order functions
[1; 2; 3] |> map (fun x -> x * 2)              # [2; 4; 6]
[1; 2; 3; 4] |> filter (fun x -> x % 2 == 0)   # [2; 4]
[1; 2; 3] |> fold 0 (fun acc x -> acc + x)     # 6

# Multi-line lambda with braces
let process = fun x -> {
    let temp = x * 2
    let adjusted = temp + 1
    adjusted
}

# Type-annotated lambda
let typedFn: (int -> int) = fun x -> x * 2
let annotatedLambda = fun (x: int) (y: int) -> x + y

# Lambdas capturing outer scope
let multiplier = 10
let scale = fun x -> x * multiplier    # Captures 'multiplier'

# Nested lambdas (currying manually)
let curriedAdd = fun x -> fun y -> x + y
let add5 = curriedAdd 5
```

### 5.4 Placeholder Lambda Sugar (`_`)

The `_` token in expression position (not pattern position) creates an implicit single-parameter lambda. This is purely a parser-level desugaring — no changes to IR or runtime.

```fsharp
# Field accessor
_.pid                         # → fun __x -> __x.pid

# Predicate
_.name == "endo"              # → fun __x -> __x.name == "endo"

# Comparison
_.cpu > 50.0                  # → fun __x -> __x.cpu > 50.0

# Arithmetic
_ + 1                         # → fun __x -> __x + 1

# In pipelines (most common use)
ps |> filter (_.name == "endo") |> map _.pid
ps |> sortBy _.cpu |> groupBy _.user

# Only one _ per expression (multiple would be ambiguous)
# BAD: _ + _                  # Error: ambiguous placeholder lambda
```

**Rule:** Any expression containing `_` in expression position (outside of pattern context such as `match` arms or `let` destructuring) creates an implicit lambda. The `_` becomes the single parameter. This sugar works anywhere a function value is expected.

### 5.5 Recursive Functions

```fsharp
# The 'rec' keyword enables recursion
let rec gcd a b =
    match b with
    | 0 -> a
    | _ -> gcd b (a % b)

let rec sumList lst =
    match lst with
    | [] -> 0
    | [x] -> x
    | head :: tail -> head + sumList tail

# Mutual recursion with 'and'
let rec isEven n =
    match n with
    | 0 -> true
    | n -> isOdd (n - 1)
and isOdd n =
    match n with
    | 0 -> false
    | n -> isEven (n - 1)

# Tail-recursive with accumulator (efficient)
let factorial n =
    let rec loop acc n =
        match n with
        | 0 -> acc
        | n -> loop (acc * n) (n - 1)
    loop 1 n

# Tree traversal
type Tree<T> =
    | Leaf of T
    | Node of Tree<T> * Tree<T>

let rec sumTree tree =
    match tree with
    | Leaf n -> n
    | Node (left, right) -> sumTree left + sumTree right
```

### 5.6 Function Composition

```fsharp
# Forward composition operator >>
let doubleAndAdd1 = double >> (add 1)
let result = doubleAndAdd1 5           # (5*2)+1 = 11

# Backward composition operator <<
let add1AndDouble = (add 1) << double
let result = add1AndDouble 5           # (5+1)*2 = 12

# Building pipelines with composition
let processNumbers = 
    filter isPositive 
    >> map double 
    >> fold 0 add

let result = processNumbers [(-1); 2; (-3); 4; 5]    # 22

# Point-free style
let normalizeAndCount = 
    trim 
    >> toLower 
    >> words 
    >> length

let wordCount = normalizeAndCount "  Hello World  "  # 2
```

---

## 6. Lists & Collections

### 6.1 List Literals

```fsharp
# Homogeneous lists with semicolon separators
let nums = [1; 2; 3; 4; 5]
let strs = ["apple"; "banana"; "cherry"]
let bools = [true; false; true]
let empty: list<int> = []

# Nested lists
let matrix = [
    [1; 2; 3]
    [4; 5; 6]
    [7; 8; 9]
]

# Range syntax
let oneToTen = [1..10]                # [1; 2; 3; 4; 5; 6; 7; 8; 9; 10]
let evens = [2; 4..20]                # [2; 4; 6; 8; ... 20]
let countdown = [10..-1..0]           # [10; 9; 8; ... 0]
let letters = ['a'..'z']              # All lowercase letters

# List comprehensions
let squares = [for x in 1..10 -> x * x]
let filtered = [for x in items when x > 5 -> x * 2]
let pairs = [for x in 1..3 -> for y in 1..3 -> (x, y)]
```

### 6.2 List Operations

```fsharp
# Basic operations
let first = head [1; 2; 3]             # 1 (or None if empty)
let rest = tail [1; 2; 3]              # [2; 3]
let len = length [1; 2; 3]             # 3
let empty = isEmpty []                 # true
let last_ = last [1; 2; 3]             # 3

# Construction
let prepend = 0 :: [1; 2; 3]           # [0; 1; 2; 3]
let concat = [1; 2] @ [3; 4]           # [1; 2; 3; 4]
let replicated = replicate 3 "x"       # ["x"; "x"; "x"]
let singleton = [42]                   # Single-element list

# Access
let third = nth 2 items                # Zero-indexed access
let slice = items[2..5]                # Slice from index 2 to 5
let firstThree = take 3 items
let afterThree = drop 3 items

# Searching
let found = find (fun x -> x > 5) nums           # Some 6
let exists_ = exists (fun x -> x > 100) nums     # false
let all_ = forall (fun x -> x > 0) nums          # true
let index = findIndex (fun x -> x == 3) nums     # Some 2

# Transformation
let doubled = map (fun x -> x * 2) nums          # Double each
let evens = filter (fun x -> x % 2 == 0) nums    # Keep evens
let sum = fold 0 (fun acc x -> acc + x) nums     # Sum all
let product = reduce (fun a b -> a * b) nums     # Multiply all
let sorted = sort nums                           # Ascending order
let reversed = reverse nums
let unique = distinct nums                       # Remove duplicates

# Combination
let zipped = zip [1; 2; 3] ["a"; "b"; "c"]       # [(1,"a"); (2,"b"); (3,"c")]
let flattened = flatten [[1;2]; [3;4]; [5;6]]   # [1; 2; 3; 4; 5; 6]
let grouped = groupBy (fun x -> x % 2) nums     # Group by odd/even
```

### 6.3 Pipeline Style (Preferred)

```fsharp
# Pipelines make transformations readable
nums 
|> filter (fun x -> x > 0)
|> map (fun x -> x * 2)
|> fold 0 (+)

# Complex data processing
let topErrors = 
    readFile "/var/log/app.log"
    |> lines
    |> filter (fun l -> contains l "ERROR")
    |> map (fun l -> extractErrorCode l)
    |> groupBy id
    |> map (fun (code, occurrences) -> (code, length occurrences))
    |> sortByDescending snd
    |> take 10

# With partial application
let processUsers = 
    filter isActive
    >> map normalizeEmail
    >> sortBy lastName
    >> take 100
```

### 6.4 Working with Command Output

```fsharp
# Convert command output to list
let files = ls | lines                 # list<str>
let procs = ps aux | lines | drop 1    # Skip header line

# Process each item
files |> each (fun f -> echo "File: $f")

# Filter and transform shell output
ls -la 
|> lines 
|> filter (fun l -> contains l ".txt")
|> map (fun l -> words l |> last)      # Get filename column
|> each (fun f -> echo "Text file: $f")

# Combine shell commands with functional processing
let largeLogFiles = 
    find /var/log -name "*.log"
    |> lines
    |> filter (fun f -> {
        let size = stat -c%s $f |> parseInt
        size > 1000000
    })
    |> map (fun f -> { path = f; size = stat -c%s $f })
    |> sortByDescending (fun r -> r.size)

# Parallel processing
files |> parallel 4 (fun f -> gzip $f)
```

---

## 7. Pattern Matching

Pattern matching is a powerful way to destructure data and make decisions based on its shape.

### 7.1 Basic Patterns

```fsharp
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

```fsharp
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

```fsharp
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

```fsharp
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

```fsharp
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

```fsharp
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

## 8. Operators & Pipelines

### 8.1 Arithmetic Operators

```fsharp
let a = 10 + 5      # Addition: 15
let b = 10 - 5      # Subtraction: 5
let c = 10 * 5      # Multiplication: 50
let d = 10 / 3      # Integer division: 3
let e = 10.0 / 3.0  # Float division: 3.333...
let f = 10 % 3      # Modulo: 1
let g = 2 ** 10     # Power: 1024
let h = -x          # Negation
```

### 8.2 Comparison Operators

```fsharp
let eq = a == b     # Equal
let ne = a != b     # Not equal
let lt = a < b      # Less than
let le = a <= b     # Less than or equal
let gt = a > b      # Greater than
let ge = a >= b     # Greater than or equal

# Comparisons return bool
if count >= 10 then
    echo "Enough items"
fi
```

### 8.3 Logical Operators

```fsharp
let and_ = a && b   # Logical AND (short-circuit)
let or_ = a || b    # Logical OR (short-circuit)
let not_ = !a       # Logical NOT

# Short-circuit evaluation
if fileExists path && isReadable path then
    # isReadable only called if fileExists returns true
    cat $path
fi

# In expressions
let canProceed = isValid && hasPermission || isAdmin
```

### 8.4 String Operators

```fsharp
let concat = "Hello, " + name         # String concatenation
let repeated = "=" * 40               # Repeat string: "====...===="
```

### 8.5 Function Pipeline Operator `|>`

The `|>` operator passes the result of the left side as the last argument to the function on the right.

```fsharp
# Basic pipeline
let result = 
    [1; 2; 3; 4; 5]
    |> filter (fun x -> x > 2)    # [3; 4; 5]
    |> map (fun x -> x * 2)       # [6; 8; 10]
    |> fold 0 (+)                 # 24

# Equivalent to nested calls (hard to read)
let result = fold 0 (+) (map (fun x -> x * 2) (filter (fun x -> x > 2) [1; 2; 3; 4; 5]))

# String processing pipeline
let cleaned = 
    "  Hello, World!  "
    |> trim                        # "Hello, World!"
    |> toLower                     # "hello, world!"
    |> replace "," ""              # "hello world!"
    |> words                       # ["hello"; "world!"]
    |> head                        # "hello"

# With partial application
let numbers = [1; 2; 3; 4; 5]
numbers 
|> map (add 10)                   # Add 10 to each
|> filter (fun x -> x > 12)       # Keep > 12
|> length                         # Count remaining

# Ending with a lambda for custom processing
ls -la
|> lines
|> filter (fun l -> endsWith l ".md")
|> length
|> fun n -> echo "Found $n markdown files"
```

### 8.6 Shell Pipeline Operator `|`

The `|` operator connects the stdout of the left process to the stdin of the right process.

```fsharp
# Basic shell pipeline
ps aux | grep nginx | wc -l

# Multi-stage pipeline
cat /var/log/syslog 
| grep ERROR 
| sort 
| uniq -c 
| sort -rn 
| head 10

# With redirects
cat input.txt | sort | uniq > output.txt

# Pipeline with background
long_running_command | processor | output_handler &
```

### 8.7 Combining `|>` and `|`

The two pipe operators can be combined for powerful data processing.

```fsharp
# Shell pipe output into function pipeline
ls -la 
| lines 
|> filter (fun l -> contains l ".rs") 
|> length
|> fun n -> echo "Found $n Rust files"

# Function result into shell pipe
["Hello"; "World"; "From"; "Endo"] 
|> unlines 
| wc -w

# Complex combination
find . -name "*.log"
| lines
|> filter (fun f -> {
    let size = stat -c%s $f |> parseInt
    size > 1000000
})
|> each (fun f -> {
    echo "Compressing $f"
    gzip $f
})

# Process shell output with F# style transformations
let summary = 
    git log --oneline
    | lines
    |> take 20
    |> map (fun l -> {
        let parts = words l
        { hash = head parts; message = tail parts |> unwords }
    })
    |> filter (fun c -> contains c.message "fix")
```

### 8.8 Operator Precedence

From lowest to highest precedence:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `\|>` | Left |
| 2 | `\|\|` | Left |
| 3 | `&&` | Left |
| 4 | `==` `!=` `<` `<=` `>` `>=` | Left |
| 5 | `+` `-` | Left |
| 6 | `*` `/` `%` | Left |
| 7 | `**` | Right |
| 8 | `!` `-` (unary) | Right |
| 9 | `.` `[]` function application | Left |

---

## 9. Control Flow

### 9.1 If Expression/Statement

`if` can be used as an expression (returns value) or statement (for effects).

```fsharp
# If as EXPRESSION (returns value, requires else)
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

# If as STATEMENT (for side effects, else optional)
if fileExists path then
    echo "File found"
    cat $path
fi

if hasErrors then
    echo "Errors detected!"
    exit 1
fi

# With else
if fileExists path then
    echo "File found"
    cat $path
else
    echo "File not found"
    exit 1
fi

# Elif chains
if status == 200 then
    echo "OK"
elif status == 404 then
    echo "Not found"
elif status >= 500 then
    echo "Server error"
else
    echo "Unknown: $status"
fi

# Single-line (semicolons optional because 'then' is keyword)
if test -f $file then cat $file fi
if test -f $file then cat $file else echo "missing" fi

# Parentheses optional around condition
if (count > 0) then process fi
if count > 0 then process fi        # Same thing
```

### 9.2 Match Expression

See [Section 7: Pattern Matching](#7-pattern-matching) for comprehensive coverage.

```fsharp
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

```fsharp
# For-in loop over list
for item in [1; 2; 3; 4; 5] do
    echo "Item: $item"
done

# For-in over range
for i in 1..10 do
    echo "Count: $i"
done

for i in 10..-1..1 do
    echo "Countdown: $i"
done

# For-in over command output
for file in $(ls *.txt) do
    echo "Processing: $file"
    wc -l $file
done

# For-in with destructuring
for (name, value) in entries do
    echo "$name = $value"
done

for { host; port } in servers do
    ping $host
done

# While loop
let mut n = 10
while n > 0 do
    echo "Countdown: $n"
    n <- n - 1
done

# Infinite loop with break
while true do
    let input = read
    if input == "quit" then break fi
    process input
done

# Break and continue
for item in items do
    if item == "skip" then continue fi
    if item == "stop" then break fi
    process item
done

# While with complex condition
while hasMoreData && !cancelled do
    processNextBatch
done
```

### 9.4 Bash-Compatible Control Flow

For compatibility, traditional bash syntax is also supported.

```fsharp
# Traditional if-then-else-fi with test
if [ -f "$file" ]; then
    cat "$file"
elif [ -d "$file" ]; then
    ls "$file"
else
    echo "Not found"
fi

# Brackets optional with endo conditions
if test -f $file then
    cat $file
fi

# Case statement (bash style)
case $command in
    start)
        start_service
        ;;
    stop)
        stop_service
        ;;
    restart)
        stop_service
        start_service
        ;;
    *)
        echo "Unknown command"
        ;;
esac

# Short-circuit operators as control flow
test -f $file && cat $file
test -f $file || echo "Not found"
command1 && command2 || fallback

# C-style for loop (coming soon)
# for ((i = 0; i < 10; i++)) do
#     echo $i
# done
```

---

## 10. Command Execution

### 10.1 Statement Context (Output to Terminal)

When a command appears as a statement (not in an expression), its output goes directly to the terminal.

```fsharp
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

```fsharp
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

```fsharp
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

```fsharp
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

```fsharp
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

```fsharp
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

## 11. Error Handling

### 11.1 Result Type

Commands in expression context return a `result` type for explicit error handling.

```fsharp
# Commands return Result<str, Error>
let result = cat nonexistent.txt

match result with
| Ok content -> echo "Content: $content"
| Error e -> echo "Error (${e.code}): ${e.message}"

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
    log "Failed: ${getError result}"
fi
```

### 11.2 Error Propagation with `?`

The `?` operator propagates errors up the call stack (similar to Rust).

```fsharp
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

```fsharp
# Basic try-with
let result = try
    let data = fetchData url?
    let parsed = parseJson data?
    processData parsed
with
| { code = 404; _ } -> 
    DefaultData.empty
| { code = c; message = m } when c >= 500 -> 
    log "Server error: $m"
    Error { code = c; message = m }
| e -> 
    Error e

# Try with specific error patterns
let config = try
    loadConfig configPath?
with
| { message = m } when contains m "not found" ->
    echo "Config not found, using defaults"
    DefaultConfig
| { message = m } when contains m "permission" ->
    echo "Cannot read config: $m"
    exit 1
| e ->
    echo "Unexpected error: ${e.message}"
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

```fsharp
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

```fsharp
# Access last exit code
someCommand
if $? == 0 then
    echo "Success"
else
    echo "Failed with code $?"
fi

# Exit with code
exit 0                            # Success
exit 1                            # General error
exit 127                          # Command not found

# Return from function with value
let checkFile path =
    if test -f $path then
        return 0
    else
        return 1
    fi

# Check command success in conditions
if grep -q pattern file.txt then
    echo "Found"
fi

# Boolean commands
if test -f $file && test -r $file then
    cat $file
fi
```

---

## 12. Modules and Imports

### 12.1 File-based Modules

```fsharp
# Import another endo script
import "./utils.endo"
import "../lib/helpers.endo"

# Import with alias
import "./database.endo" as db
db.connect "localhost"
db.query "SELECT * FROM users"

# Selective import
from "./math.endo" import (add, multiply, PI)
from "./utils.endo" import *

# Conditional import
if production then
    import "./config.prod.endo" as config
else
    import "./config.dev.endo" as config
fi
```

### 12.2 Standard Library

```fsharp
# Core functions are always available
# No import needed for: map, filter, fold, head, tail, etc.

# Module-qualified access for clarity
let numbers = [1; 2; 3; 4; 5]
let sum = List.fold 0 (+) numbers
let doubled = List.map (fun x -> x * 2) numbers

let text = "hello,world,foo"
let parts = String.split "," text     # ["hello"; "world"; "foo"]
let joined = String.join "-" parts    # "hello-world-foo"

# File operations
let content = File.read "data.txt"
File.write "output.txt" processedData
let files = File.list "."

# Path operations
let full = Path.join [homeDir; "documents"; "file.txt"]
let ext = Path.extension "file.txt"   # ".txt"
let base_ = Path.basename "/a/b/c.txt" # "c.txt"
```

### 12.3 Creating Modules

```fsharp
# mymodule.endo

# Public exports (default)
let add x y = x + y
let multiply x y = x * y

# Private (underscore prefix convention)
let _helper x = x * 2

# Export types
type Point = { x: float; y: float }

# Module initialization
let _init = echo "Module loaded"
```

---

## 13. Interoperability: F# Style vs Bash Style

### 13.1 When to Use Each Style

| Task | Recommended Style | Example |
|------|-------------------|---------|
| Data transformation | F# style | `data \|> map transform \|> filter valid` |
| Process pipelines | Bash style | `ps aux \| grep nginx \| wc -l` |
| Configuration | F# records | `{ host = "localhost"; port = 8080 }` |
| Quick scripts | Bash style | `for f in *.txt; do cat $f; done` |
| Complex logic | F# match | `match result with \| Ok x -> ... \| Error e -> ...` |
| Simple conditionals | Bash style | `test -f $file && cat $file` |
| Complex conditionals | F# style | `if complex_expr then branch1 else branch2` |
| Error handling | F# style | `let result = operation?; match result with ...` |
| One-off commands | Bash style | `ls -la; git status` |
| Reusable functions | F# style | `let process x = x \|> transform \|> validate` |

### 13.2 Mixing Styles

```fsharp
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
done

# Conditional mixing F# and bash styles
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
fi

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

```fsharp
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

### 13.4 Function and Command Resolution

When you call something, endo resolves it in this order:

1. **User-defined functions** (current scope)
2. **Imported functions** (from modules)
3. **Built-in functions** (map, filter, etc.)
4. **Shell builtins** (cd, export, etc.)
5. **External commands** (PATH lookup)

```fsharp
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

### 13.5 Transitioning from Bash

Common patterns and their endo equivalents:

```fsharp
# Bash: VAR="value"
# Endo:
let var = "value"

# Bash: export VAR="value"
# Endo:
export VAR = "value"

# Bash: if [ -f "$file" ]; then cat "$file"; fi
# Endo:
if test -f $file then cat $file fi
# or
if fileExists file then cat $file fi

# Bash: for f in *.txt; do echo "$f"; done
# Endo:
for f in $(ls *.txt) do echo $f done
# or 
ls *.txt | lines |> each echo

# Bash: result=$(command)
# Endo:
let result = command

# Bash: command1 && command2 || command3
# Endo (same):
command1 && command2 || command3
# or F# style:
if command1 then command2 else command3 fi

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
```

---

## 14. Implementation Notes for Parser

### 14.1 Lexer Modifications

New tokens required for F# syntax:

```cpp
enum class Token {
    // ... existing tokens ...
    
    // F# style keywords
    Let,              // 'let'
    Mut,              // 'mut'
    Fun,              // 'fun'
    Match,            // 'match'
    With,             // 'with'
    When,             // 'when'
    Type,             // 'type'
    Of,               // 'of'
    Rec,              // 'rec'
    And,              // 'and' (mutual recursion)
    As,               // 'as' (pattern alias)
    In,               // 'in' (let...in)
    
    // New operators
    Arrow,            // '->'
    FatArrow,         // '=>'
    LeftArrow,        // '<-'
    ForwardPipe,      // '|>'
    Composition,      // '>>'
    BackComposition,  // '<<'
    DoubleColon,      // '::'
    DoubleStar,       // '**'
    QuestionMark,     // '?'
    QuestionPipe,     // '?|'
    QuestionDot,      // '?.'
    
    // List delimiters
    // Note: Semicolon already exists but now also separates list elements
};
```

### 14.2 Grammar Ambiguities and Resolution

**Challenge 1: `let` binding vs bash assignment**
```fsharp
let x = 42          # F# style (new)
x=42                # Bash style (existing)
```
**Resolution:** The `let` keyword unambiguously starts F# style. Bash style requires no spaces around `=`.

**Challenge 2: Pipe operators `|` vs `|>`**
```fsharp
cmd1 | cmd2         # Shell pipe (process stdout -> stdin)
data |> func        # Function pipe (value -> function)
```
**Resolution:** Lexer emits different tokens. `|` followed by `>` produces `ForwardPipe`, otherwise `Pipe`.

**Challenge 3: Function call vs command**
```fsharp
let x = foo bar     # Is foo a function or command?
```
**Resolution:** Unified semantics - resolved at runtime based on resolution order. Parser treats all calls uniformly as applications.

**Challenge 4: Semicolons in different contexts**
```fsharp
[1; 2; 3]           # List element separator
cmd1; cmd2          # Statement separator
if cond then        # No semicolon needed (optional)
```
**Resolution:** Context-sensitive parsing. Inside `[]`, semicolon separates list elements. At statement level, separates statements.

**Challenge 5: Pattern vs Expression in match arms**
```fsharp
match x with
| pattern -> expr   # pattern is NOT an expression
```
**Resolution:** After `|` in match context, parse as pattern (different grammar rules). After `->`, parse as expression.

### 14.3 Parser Structure

```cpp
class Parser {
    // ... existing methods ...
    
    // F# style additions
    std::unique_ptr<Stmt> parseLetBinding();
    std::unique_ptr<Stmt> parseFunctionDef();
    std::unique_ptr<Stmt> parseTypeDefinition();
    
    std::unique_ptr<Expr> parseLambda();
    std::unique_ptr<Expr> parseMatchExpr();
    std::unique_ptr<Expr> parseListLiteral();
    std::unique_ptr<Expr> parseRecordLiteral();
    std::unique_ptr<Expr> parsePipelineExpr();
    std::unique_ptr<Expr> parseApplicationExpr();
    
    // Pattern parsing
    std::unique_ptr<Pattern> parsePattern();
    std::unique_ptr<Pattern> parseOrPattern();
    std::unique_ptr<Pattern> parseConsPattern();
    std::unique_ptr<Pattern> parsePrimaryPattern();
    
    // Helper for curried parameters
    std::vector<Parameter> parseFunctionParams();
    
    // Type parsing
    std::unique_ptr<Type> parseType();
    std::unique_ptr<Type> parseTypeAtom();
    std::optional<Type> parseOptionalTypeAnnotation();
};
```

### 14.4 AST Extensions

```cpp
// New statement nodes
struct LetBindingStmt : Stmt {
    std::unique_ptr<Pattern> pattern;
    bool isMutable;
    std::optional<Type> typeAnnotation;
    std::unique_ptr<Expr> value;
};

struct FunctionDefStmt : Stmt {
    std::string name;
    bool isRecursive;
    std::vector<Parameter> params;
    std::optional<Type> returnType;
    std::unique_ptr<Expr> body;
};

struct TypeDefStmt : Stmt {
    std::string name;
    std::vector<std::string> typeParams;
    std::unique_ptr<TypeBody> body;  // RecordType or UnionType
};

// New expression nodes
struct LambdaExpr : Expr {
    std::vector<Parameter> params;
    std::unique_ptr<Expr> body;
};

struct MatchExpr : Expr {
    std::unique_ptr<Expr> scrutinee;
    std::vector<MatchArm> arms;
};

struct MatchArm {
    std::unique_ptr<Pattern> pattern;
    std::optional<std::unique_ptr<Expr>> guard;  // when clause
    std::unique_ptr<Expr> body;
};

struct ListExpr : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    // Or range: start, end, step
    std::optional<RangeSpec> range;
};

struct RecordExpr : Expr {
    std::optional<std::unique_ptr<Expr>> baseRecord;  // { x with ... }
    std::vector<FieldAssignment> fields;
};

struct PipelineExpr : Expr {
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    enum class Kind { Forward, Shell } kind;  // |> vs |
};

struct ApplicationExpr : Expr {
    std::unique_ptr<Expr> function;
    std::vector<std::unique_ptr<Expr>> args;
};

struct PropagateExpr : Expr {
    std::unique_ptr<Expr> inner;  // The ? operator
};

// Pattern AST
struct Pattern { virtual ~Pattern() = default; };

struct LiteralPattern : Pattern { 
    Value value; 
};

struct VariablePattern : Pattern { 
    std::string name; 
};

struct WildcardPattern : Pattern {};

struct TuplePattern : Pattern { 
    std::vector<std::unique_ptr<Pattern>> elements; 
};

struct ListPattern : Pattern { 
    std::vector<std::unique_ptr<Pattern>> elements;
    std::optional<std::string> rest;  // for [head; tail...]
};

struct ConsPattern : Pattern {
    std::unique_ptr<Pattern> head;
    std::unique_ptr<Pattern> tail;
};

struct RecordPattern : Pattern {
    std::vector<FieldPattern> fields;
    bool hasWildcard;  // { name; _ }
};

struct ConstructorPattern : Pattern {
    std::string name;  // Some, None, Ok, Error, or user-defined
    std::optional<std::unique_ptr<Pattern>> payload;
};

struct AsPattern : Pattern {
    std::unique_ptr<Pattern> inner;
    std::string name;
};

struct OrPattern : Pattern {
    std::vector<std::unique_ptr<Pattern>> alternatives;
};
```

### 14.5 Type Inference Engine

```cpp
class TypeInference {
public:
    // Entry point: infer type of expression in environment
    Type infer(const Expr& expr, TypeEnv& env);
    
    // Check expression against expected type
    bool check(const Expr& expr, Type expected, TypeEnv& env);
    
    // Unification: find substitution making types equal
    std::optional<Substitution> unify(Type a, Type b);
    
    // Apply substitution to type
    Type apply(const Substitution& subst, Type t);
    
private:
    int freshVarCounter = 0;
    
    // Create fresh type variable
    Type freshTypeVar() { 
        return TypeVar("t" + std::to_string(freshVarCounter++)); 
    }
    
    // Specific inference rules
    Type inferLet(const LetBindingStmt& let, TypeEnv& env);
    Type inferLambda(const LambdaExpr& lambda, TypeEnv& env);
    Type inferApplication(const ApplicationExpr& app, TypeEnv& env);
    Type inferMatch(const MatchExpr& match, TypeEnv& env);
    Type inferList(const ListExpr& list, TypeEnv& env);
    Type inferRecord(const RecordExpr& record, TypeEnv& env);
    Type inferPipeline(const PipelineExpr& pipeline, TypeEnv& env);
    
    // Pattern type checking (returns bindings introduced)
    TypeEnv checkPattern(const Pattern& pat, Type expectedType, TypeEnv& env);
    
    // Generalization for let-polymorphism
    TypeScheme generalize(Type t, const TypeEnv& env);
    Type instantiate(const TypeScheme& scheme);
};

// Type representation
struct Type {
    std::variant<
        PrimitiveType,      // int, str, bool, etc.
        TypeVar,            // 'a, 'b (inference variables)
        FunctionType,       // T -> U
        ListType,           // list<T>
        TupleType,          // (T, U, V)
        RecordType,         // { field: T; ... }
        UnionType,          // Case1 of T | Case2 of U
        OptionType,         // option<T>
        ResultType          // result<T, E>
    > inner;
};

struct TypeScheme {
    std::vector<std::string> quantified;  // Bound type variables
    Type body;
};
```

### 14.6 IR Generation Extensions

```cpp
class IRGenerator : public Visitor {
    // ... existing methods ...
    
    // New visit methods
    void visit(const LetBindingStmt& stmt) override;
    void visit(const FunctionDefStmt& stmt) override;
    void visit(const LambdaExpr& expr) override;
    void visit(const MatchExpr& expr) override;
    void visit(const ListExpr& expr) override;
    void visit(const RecordExpr& expr) override;
    void visit(const PipelineExpr& expr) override;
    
    // Pattern compilation (generates decision tree)
    void compilePattern(const Pattern& pat, Register scrutinee, Label onMatch, Label onFail);
    void compileMatchArms(const std::vector<MatchArm>& arms, Register scrutinee);
    
    // Closure handling
    std::vector<std::string> findFreeVariables(const LambdaExpr& lambda);
    void emitClosure(const LambdaExpr& lambda, const std::vector<std::string>& freeVars);
};
```

---

## 15. EBNF Grammar

```ebnf
(* ============================================ *)
(* Endo Language Grammar - EBNF Specification  *)
(* ============================================ *)

(* ---------- Top-level ---------- *)

program         = { statement } ;

(* ---------- Statements ---------- *)

statement       = let_statement
                | type_definition
                | if_statement
                | for_statement
                | while_statement
                | match_statement
                | try_statement
                | return_statement
                | break_statement
                | continue_statement
                | export_statement
                | import_statement
                | expression_statement
                ;

let_statement   = "let" [ "export" ] [ "rec" ] [ "mut" ] let_binding { "and" let_binding } ;
let_binding     = pattern [ type_annotation ] "=" expression
                | identifier { pattern } [ type_annotation ] "=" expression
                ;

type_definition = "type" identifier [ type_params ] "=" type_body ;
type_params     = "<" identifier { "," identifier } ">" ;
type_body       = record_type | union_type | type ;

record_type     = "{" field_def { ";" field_def } [ ";" ] "}" ;
field_def       = identifier ":" type ;

union_type      = [ "|" ] union_case { "|" union_case } ;
union_case      = identifier [ "of" type ] ;

if_statement    = "if" expression [ ";" ] "then" block
                  { "elif" expression [ ";" ] "then" block }
                  [ "else" block ] "fi" ;

for_statement   = "for" pattern "in" expression [ ";" ] "do" block "done" ;

while_statement = "while" expression [ ";" ] "do" block "done" ;

match_statement = "match" expression "with" { match_arm } ;
match_arm       = "|" pattern [ "when" expression ] "->" block_or_expr ;

try_statement   = "try" block "with" { match_arm } [ "finally" block ] ;

return_statement   = "return" [ expression ] ;
break_statement    = "break" ;
continue_statement = "continue" ;
export_statement   = "export" identifier [ "=" expression ] ;
import_statement   = "import" string_literal [ "as" identifier ]
                   | "from" string_literal "import" import_list ;
import_list        = "(" identifier { "," identifier } ")" | "*" ;

expression_statement = expression ;

block           = { statement [ ";" ] } ;
block_or_expr   = block | expression ;

(* ---------- Types ---------- *)

type            = function_type ;
function_type   = tuple_type [ "->" function_type ] ;
tuple_type      = type_application { "," type_application } ;
type_application = type_atom [ "<" type { "," type } ">" ] ;
type_atom       = "int" | "float" | "str" | "bool" | "unit"
                | "list" | "option" | "result"
                | identifier
                | "(" type ")"
                ;

type_annotation = ":" type ;

(* ---------- Expressions ---------- *)

expression      = let_in_expression ;

let_in_expression = "let" pattern "=" expression "in" expression
                  | lambda_expression
                  ;

lambda_expression = "fun" { pattern } "->" expression
                  | if_expression
                  ;

if_expression   = "if" expression "then" expression "else" expression
                | match_expression
                ;

match_expression = "match" expression "with" { match_arm }
                 | pipeline_expression
                 ;

pipeline_expression = logical_or { "|>" logical_or } ;

logical_or      = logical_and { "||" logical_and } ;
logical_and     = comparison { "&&" comparison } ;
comparison      = additive { comparison_op additive } ;
additive        = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  = power { ( "*" | "/" | "%" ) power } ;
power           = unary [ "**" power ] ;
unary           = [ "!" | "-" ] postfix ;
postfix         = application { postfix_op } ;
postfix_op      = "?" | "." identifier | "[" expression "]" | "?." identifier | "?|" expression ;
application     = primary { primary } ;

primary         = literal
                | identifier
                | "_"
                | "(" expression ")"
                | "(" expression "," expression { "," expression } ")"
                | list_expression
                | record_expression
                | command_substitution
                | variable_expansion
                ;

literal         = integer_literal
                | float_literal
                | string_literal
                | "true" | "false"
                | "()"
                ;

list_expression = "[" [ list_elements ] "]" ;
list_elements   = expression { ";" expression } [ ";" ]
                | expression ".." expression [ ".." expression ]
                | "for" pattern "in" expression [ "when" expression ] "->" expression
                ;

record_expression = "{" [ record_base ] field_assign { ";" field_assign } [ ";" ] "}" ;
record_base     = expression "with" ;
field_assign    = identifier [ "=" expression ] ;

command_substitution = "$(" pipeline ")" | "`" { command_char } "`" ;
variable_expansion   = "$" identifier
                     | "${" expansion_body "}"
                     | "$((" arithmetic_expr "))"
                     ;
expansion_body  = identifier [ expansion_op ] ;
expansion_op    = ":-" word | ":+" word | "#" pattern_str | "%" pattern_str 
                | "/" pattern_str "/" word ;

(* ---------- Patterns ---------- *)

pattern         = or_pattern ;
or_pattern      = as_pattern { "|" as_pattern } ;
as_pattern      = cons_pattern [ "as" identifier ] ;
cons_pattern    = primary_pattern { "::" primary_pattern } ;

primary_pattern = "_"
                | literal
                | identifier
                | identifier primary_pattern
                | "(" pattern ")"
                | "(" pattern "," pattern { "," pattern } ")"
                | "[" [ list_pattern_elements ] "]"
                | "{" [ record_pattern_fields ] "}"
                ;

list_pattern_elements = pattern { ";" pattern } [ ";" pattern "..." ] ;
record_pattern_fields = field_pattern { ";" field_pattern } [ ";" "_" ] ;
field_pattern   = identifier [ "=" pattern ] ;

(* ---------- Commands and Pipelines ---------- *)

pipeline        = command { "|" command } [ "&" ] ;
command         = simple_command { redirect } ;
simple_command  = word { word } ;

redirect        = ">" word
                | ">>" word
                | "<" word
                | "2>" word
                | "2>>" word
                | "2>&1"
                | "&>" word
                | "<<<" word
                | "<<" heredoc_delimiter
                ;

word            = bare_word | string_literal | variable_expansion | glob_pattern ;

heredoc_delimiter = identifier | "'" identifier "'" ;

(* ---------- Lexical Elements ---------- *)

identifier      = ( letter | "_" ) { letter | digit | "_" } ;
bare_word       = ( letter | digit | "_" | "-" | "." | "/" | "~" )+ ;
glob_pattern    = { glob_char | "[" char_class "]" } ;
glob_char       = "*" | "?" | any_char ;
char_class      = { char_range | single_char } ;
char_range      = single_char "-" single_char ;

integer_literal = [ "-" ] digits
                | "0x" hex_digits
                | "0o" octal_digits
                | "0b" binary_digits
                ;

float_literal   = [ "-" ] digits "." digits [ exponent ] ;
exponent        = ( "e" | "E" ) [ "+" | "-" ] digits ;

string_literal  = double_string | single_string ;
double_string   = '"' { string_char | escape_seq | interpolation } '"' ;
single_string   = "'" { any_char_except_quote } "'" ;
escape_seq      = "\\" ( "n" | "t" | "r" | "\\" | '"' | "$" ) ;
interpolation   = "$" identifier
                | "${" expression "}"
                | "$(" pipeline ")"
                | "$((" arithmetic_expr "))"
                ;

comparison_op   = "==" | "!=" | "<" | "<=" | ">" | ">=" ;

letter          = "a".."z" | "A".."Z" ;
digit           = "0".."9" ;
digits          = digit { digit } ;
hex_digits      = hex_digit { hex_digit } ;
hex_digit       = digit | "a".."f" | "A".."F" ;
octal_digits    = octal_digit { octal_digit } ;
octal_digit     = "0".."7" ;
binary_digits   = binary_digit { binary_digit } ;
binary_digit    = "0" | "1" ;

(* ---------- Comments ---------- *)

comment         = "#" { any_char } newline
                | "//" { any_char } newline
                | "(*" { any_char | comment } "*)"
                ;
```

---

## Appendix: Quick Reference Card

```
╔══════════════════════════════════════════════════════════════════════╗
║                     ENDO LANGUAGE QUICK REFERENCE                    ║
╠══════════════════════════════════════════════════════════════════════╣
║ BINDINGS                                                             ║
║   let x = 42                    Immutable binding                    ║
║   let mut x = 42                Mutable binding                      ║
║   let export X = 42             Bind and export as env var           ║
║   x <- x + 1                    Mutation                             ║
║   let (a, b) = tuple            Destructuring                        ║
╠══════════════════════════════════════════════════════════════════════╣
║ FUNCTIONS                                                            ║
║   let f x y = x + y             Named function (curried)             ║
║   let f = fun x -> x * 2        Lambda                               ║
║   _.field, _ + 1                Placeholder lambda sugar              ║
║   let add5 = add 5              Partial application                  ║
║   f >> g                        Forward composition                  ║
╠══════════════════════════════════════════════════════════════════════╣
║ TYPES                                                                ║
║   int, float, str, bool, unit   Primitives                           ║
║   list<T>, option<T>            Generics                             ║
║   result<T, E>                  Error handling                       ║
║   { name: str; age: int }       Record                               ║
║   | Case1 | Case2 of T          Union                                ║
╠══════════════════════════════════════════════════════════════════════╣
║ LISTS                                                                ║
║   [1; 2; 3]                     List literal                         ║
║   [1..10]                       Range                                ║
║   head :: tail                  Cons                                 ║
║   list1 @ list2                 Concatenate                          ║
║   [for x in xs -> x * 2]        Comprehension                        ║
╠══════════════════════════════════════════════════════════════════════╣
║ PATTERN MATCHING                                                     ║
║   match x with                                                       ║
║   | pattern -> result           Basic arm                            ║
║   | p when guard -> result      With guard                           ║
║   | p1 | p2 -> result           Or pattern                           ║
║   | p as name -> result         As pattern                           ║
╠══════════════════════════════════════════════════════════════════════╣
║ PIPELINES                                                            ║
║   x |> f |> g                   Function pipeline                    ║
║   cmd1 | cmd2                   Shell pipeline                       ║
║   cmd | lines |> map f          Mixed                                ║
╠══════════════════════════════════════════════════════════════════════╣
║ CONTROL FLOW                                                         ║
║   if cond then a else b         If expression                        ║
║   if cond then block fi         If statement                         ║
║   for x in xs do block done     For loop                             ║
║   while cond do block done      While loop                           ║
╠══════════════════════════════════════════════════════════════════════╣
║ ERROR HANDLING                                                       ║
║   let x = cmd?                  Propagate error                      ║
║   Ok value, Error e             Result constructors                  ║
║   Some x, None                  Option constructors                  ║
║   try block with | e -> ...     Try-with                             ║
╠══════════════════════════════════════════════════════════════════════╣
║ SHELL FEATURES                                                       ║
║   $VAR, ${VAR}                  Variable expansion                   ║
║   ${VAR:-default}               Default value                        ║
║   $(command)                    Command substitution                 ║
║   > >> < 2>&1                   Redirections                         ║
║   <<EOF ... EOF                 Here document                        ║
║   <(cmd) >(cmd)                 Process substitution                 ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

*This specification is a living document. As endo evolves, this document will be updated to reflect new features and refinements.*
