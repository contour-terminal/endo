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

---
**See also:** [Lexical Elements](lexical-elements.md) | [Variables & Bindings](variables-and-bindings.md) | [Pattern Matching](pattern-matching.md)
