# Lists & Collections

### 6.1 List Literals

```endo
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

# Range syntax (brackets optional)
let oneToTen = [1..10]                # [1; 2; 3; 4; 5; 6; 7; 8; 9; 10]
let alsoOneToTen = 1..10              # Same — bare range works as standalone expression
let evens = [2; 4..20]                # [2; 4; 6; 8; ... 20]
let countdown = [10..-1..0]           # [10; 9; 8; ... 0]
let countdown2 = 10..-1..0            # Same without brackets
let letters = ['a'..'z']              # All lowercase letters

# List comprehensions
let squares = [for x in 1..10 -> x * x]
let filtered = [for x in items when x > 5 -> x * 2]
let pairs = [for x in 1..3 -> for y in 1..3 -> (x, y)]
```

### 6.2 List Operations

```endo
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

```endo
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

```endo
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
**See also:** [Type System](type-system.md) | [Operators & Pipelines](operators-and-pipelines.md) | [Pattern Matching](pattern-matching.md)
