# Operators & Pipelines

### 8.1 Arithmetic Operators

<!-- endo-no-check -->
```endo
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

<!-- endo-no-check -->
```endo
let eq = a == b     # Equal
let ne = a != b     # Not equal
let ne2 = a <> b    # Not equal (F# style, same as !=)
let lt = a < b      # Less than
let le = a <= b     # Less than or equal
let gt = a > b      # Greater than
let ge = a >= b     # Greater than or equal

# Comparisons return bool
if count >= 10 then println "Enough items"
```

### 8.3 Logical Operators

<!-- endo-no-check -->
```endo
let and_ = a && b   # Logical AND (short-circuit)
let or_ = a || b    # Logical OR (short-circuit)
let not_ = !a       # Logical NOT

# Short-circuit evaluation
if fileExists path && isReadable path then
    cat $path    # isReadable only called if fileExists returns true

# In expressions
let canProceed = isValid && hasPermission || isAdmin
```

### 8.4 String Operators

<!-- endo-no-check -->
```endo
let concat = "Hello, " + name         # String concatenation
let repeated = "=" * 40               # Repeat string: "====...===="
```

### 8.5 Function Pipeline Operator `|>`

The `|>` operator passes the result of the left side as the last argument to the function on the right.

<!-- endo-no-check -->
```endo
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
|> fun n -> println $"Found {n} markdown files"
```

### 8.6 Shell Pipeline Operator `|`

The `|` operator connects the stdout of the left process to the stdin of the right process.

<!-- endo-no-check -->
```endo
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

<!-- endo-no-check -->
```endo
# Shell pipe output into function pipeline
ls -la
| lines
|> filter (fun l -> contains l ".rs")
|> length
|> fun n -> println $"Found {n} Rust files"

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
    echo $"Compressing {f}"
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
| 5 | `..` (range) | None |
| 6 | `+` `-` | Left |
| 7 | `*` `/` `%` | Left |
| 8 | `**` | Right |
| 9 | `!` `-` (unary) | Right |
| 10 | `.` `[]` function application | Left |

---
**See also:** [Functions](functions.md) | [Lists & Collections](lists-and-collections.md) | [Command Execution](command-execution.md)
