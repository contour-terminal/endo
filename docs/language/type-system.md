# Type System

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

<!-- endo-no-check -->
```endo
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

# Lazy (deferred computation with memoization)
lazy<int>           # lazy 42
lazy<str>           # lazy (computeString data)
```

### 3.3 Type Inference Examples

<!-- endo-no-check -->
```endo
# Types are inferred automatically
let x = 42                    # x: int
let name = "Alice"            # name: str
let items = [1; 2; 3]         # items: list<int>
let pair = (1, "hello")       # pair: (int, str)
let double = fun x -> x * 2   # double: int -> int

# Inference through usage
let add x y = x + y           # add: int -> int -> int (inferred from +)
let greet name = $"Hi, {name}"  # greet: str -> str

# Explicit annotations when needed
let count: int = 42
let ratio: float = 42.0       # Would be int without annotation
let empty: list<str> = []     # Empty list needs type hint

# Function annotations
let add (x: int) (y: int): int = x + y
let parse (s: str): result<int, str> = tryParseInt s
```

### 3.4 Records

Records are named collections of fields. They provide structured data with named access.

<!-- endo-no-check -->
```endo
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

<!-- endo-no-check -->
```endo
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

# Access named fields directly with dot notation
c.radius         # 5.0
r.width          # 10.0

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

### 3.6 Generic Types

User-defined record and union types can be parameterized with type variables, enabling reusable data structures.

Type parameters are declared with the `'name` syntax in angle brackets after the type name:

<!-- endo-no-check -->
```endo
# Generic union type
type Box<'a> =
    | Wrap of 'a
    | Empty

let b1 = Wrap 42       # Box<int>
let b2 = Wrap "hello"  # Box<str>

match b1 with
| Wrap x -> print x    # 42
| Empty -> print "empty"

# Multi-parameter generic union
type Either<'a, 'b> =
    | Left of 'a
    | Right of 'b

let e = Left 42
match e with
| Left x -> print x
| Right _ -> print "other"

# Generic record type
type Pair<'a, 'b> = { first: 'a; second: 'b }

let p = { first = 1; second = "hello" }
print p.first    # 1

# Recursive generic type (binary tree)
type Tree<'a> =
    | Leaf of 'a
    | Node of Tree<'a> * Tree<'a>

let t = Node (Leaf 1, Node (Leaf 2, Leaf 3))
```

**Type erasure**: All instantiations of a generic type share a single runtime type ID. Type parameters exist only at the type-checking level and are erased before code generation. This matches how built-in types like `option<T>` and `result<T, E>` work.

**Type inference**: Constructor usage infers type parameters automatically — no explicit type application needed at call sites.

### 3.7 Dot Property Access

Built-in types expose convenient dot properties for common queries.

<!-- endo-no-check -->
```endo
# Tuple element access (by name or numeric index)
let pair = (42, "hello")
print (pair.fst)                      # 42
print (pair.0)                        # 42 (same as .fst)
print (pair.snd)                      # hello
print (pair.1)                        # hello (same as .snd)

let triple = (1, 2, 3)
print (triple.fst)                    # 1
print (triple.0)                      # 1 (same as .fst)
print (triple.snd)                    # 2
print (triple.1)                      # 2 (same as .snd)
print (triple.trd)                    # 3
print (triple.2)                      # 3 (same as .trd)

# Option properties
let opt = Some 42
print opt.isSome                      # true
print opt.isNone                      # false

# Result properties
let ok = Ok 100
print ok.isOk                         # true
print ok.isError                      # false

# String properties
let s = "hello"
print s.length                        # 5
```

### 3.8 Built-in Record Types

Endo provides several built-in record types for shell data. These are returned by
built-in commands and support dot access, pattern matching, and pipeline operations.

#### `Size`

Represents byte quantities with human-readable display.

| Field | Type | Description |
|-------|------|-------------|
| `bytes` | `int` | Raw byte count |

**Constructors:** `Size.fromBytes`, `Size.fromKB`, `Size.fromMB`, `Size.fromGB`, `Size.fromTB`

**Literals:** `42B`, `1KB`, `5MB`, `2GB`, `1TB`, `3.5KB`

#### `DateTime`

Represents a point in time (UTC).

| Field | Type | Description |
|-------|------|-------------|
| `year` | `int` | Year (e.g. 2024) |
| `month` | `int` | Month (1--12) |
| `day` | `int` | Day of month (1--31) |
| `hour` | `int` | Hour (0--23) |
| `minute` | `int` | Minute (0--59) |
| `second` | `int` | Second (0--59) |
| `epoch` | `int` | Unix epoch timestamp |

**Constructors:** `DateTime.now`, `DateTime.fromEpoch`

#### `TimeSpan`

Represents a duration of time with millisecond precision.

| Field | Type | Description |
|-------|------|-------------|
| `milliseconds` | `int` | Duration in milliseconds |

**Constructors:** `TimeSpan.fromMilliseconds`, `TimeSpan.fromSeconds`, `TimeSpan.fromMinutes`, `TimeSpan.fromHours`, `TimeSpan.fromDays`

**Literals:** `100ms`, `5s`, `2min`, `1h`, `1.5h`

#### `FileInfo`

Returned by `ls`. Represents a file or directory entry.

| Field | Type | Description |
|-------|------|-------------|
| `name` | `str` | File name |
| `size` | `Size` | File size |
| `mode` | `FileMode` | Unix file permissions |
| `mtime` | `DateTime` | Last modification time |
| `isDir` | `bool` | Whether entry is a directory |

#### `FileMode`

Represents Unix file permissions with human-readable display.

| Field | Type | Description |
|-------|------|-------------|
| `bits` | `int` | Raw permission bits |

**Computed properties:** `isReadable : bool`, `isWritable : bool`, `isExecutable : bool`, `owner : int` (0-7), `group : int` (0-7), `other : int` (0-7).

**Constructor:** `FileMode.fromBits n`

#### `ProcessInfo`

Returned by `ps`. Represents a running process.

| Field | Type | Description |
|-------|------|-------------|
| `pid` | `int` | Process ID |
| `ppid` | `int` | Parent process ID |
| `user` | `str` | Owning user |
| `cpu` | `float` | CPU usage percentage |
| `mem` | `float` | Memory usage percentage |
| `command` | `str` | Command name |

#### `JobInfo`

Returned by `jobs`. Represents a background job.

| Field | Type | Description |
|-------|------|-------------|
| `id` | `int` | Job number |
| `state` | `str` | Job state |
| `command` | `str` | Command string |
| `pid` | `int` | Process ID |

---
**See also:** [Lexical Elements](lexical-elements.md) | [Variables & Bindings](variables-and-bindings.md) | [Pattern Matching](pattern-matching.md)
