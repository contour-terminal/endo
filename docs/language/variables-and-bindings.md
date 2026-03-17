# Variables & Bindings

### 4.1 Immutable Bindings

By default, `let` creates immutable bindings that cannot be reassigned.

<!-- endo-no-check -->
```endo
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

<!-- endo-no-check -->
```endo
# Mutable binding
let mut counter = 0
let mut name = "initial"

# Reassign with <- operator
counter <- counter + 1
counter <- counter + 1
println $"Counter: {counter}"    # Counter: 2

name <- "updated"

# Mutable variables in loops
let mut sum = 0
for n in [1; 2; 3; 4; 5] do
    sum <- sum + n
end
println $"Sum: {sum}"            # Sum: 15

# Mutable is required for accumulation patterns
let mut result = []
for line in $(cat file.txt) | lines do
    if startsWith line "#" then
        result <- result @ [line]
end
```

The `<-` operator can also be used in expression context, returning unit:

```endo
let mut x = 0
if true then x <- 42
print x
```

### 4.3 Export Bindings

Use `let export` to bind a value and simultaneously export it as an environment variable.
The expression is evaluated, bound as a normal F# variable, and its string representation
is exported to the environment.

<!-- endo-no-check -->
```endo
# Export a number — binds X = 42 and exports X="42"
let export X = 42

# Export a computed value
let export PATH_COUNT = length (split PATH ":")

# Export with mutable binding — mutations automatically re-export
let export mut LEVEL = 1
LEVEL <- LEVEL + 1            # Re-exports LEVEL="2" to the environment

# String and boolean exports
let export GREETING = "hello"
let export VERBOSE = true     # exports as "true"
```

> **Note:** Only scalar types (string, number, float, bool) can be exported.
> Compound types (list, tuple, option, result) produce a compile error.
> Use `|> join ":"` to convert lists:
>
> ```endo
> let export PATH = ["/bin"; "/usr/bin"; "/usr/local/bin"] |> join ":"
> ```
>
> `let export rec` is not allowed — functions cannot be exported.
> Mutations to `let export mut` variables automatically re-export the updated value.

### 4.3.1 Computed Properties

Use `let get` to declare a binding that re-evaluates its body on every reference.
Unlike value bindings (`let x = expr`) which evaluate once, `let get` bindings
execute their body each time the name is accessed — no `()` call syntax needed.

<!-- endo-no-check -->
```endo
# Re-evaluating binding (short form)
let get greeting = & echo hello
print greeting                        # runs 'echo hello' each time

# Useful for shell aliases
let get cdp = & cd ~/projects
cdp                                   # changes directory

# Re-evaluation with mutable state
let mut counter = 0
let get next =
    counter <- counter + 1
    counter
print next                            # 1
print next                            # 2

# Works with export and private modifiers
let export get answer = 42
let private get helper = computeValue
```

> **Note:** `let get name = expr` is syntactic sugar for `let name with get () = expr`.
> The `get` modifier is mutually exclusive with `mut`, `rec`, `use`, and `manual`.

### 4.3.2 Properties with Get/Set Accessors

For read-write properties that need both getter and setter logic, use the full
`with get`/`set` syntax.

<!-- endo-no-check -->
```endo
# Read-only computed property (full form)
let Pi with get () = 3.14159

# Read-write property backed by a mutable variable
let mutable _counter = 0
let Counter with
    get () = _counter
    and set (v) = _counter <- v

print Counter                         # 0
Counter <- 42
print Counter                         # 42

# Write-only property
let Logger with set (msg) =
    println $"[LOG] {msg}"
```

**Multi-line bodies** work the same as function bodies — indent the body further than the `let` keyword:

<!-- endo-no-check -->
```endo
let mutable _x = 0
let mutable _log = 0
let X with
    get () =
        let v = _x
        v
    and set (v) =
        _log <- _log + 1
        _x <- v
```

The `with` keyword may also appear on the line following the property name:

<!-- endo-no-check -->
```endo
let X
    with get () = _x
    and set (v) = _x <- v
```

### 4.3.3 Builtin Properties

The shell provides builtin properties for configuration that use the same `<-` assignment
syntax as mutable variables. Unlike user-defined properties, these are registered by the
runtime and available without declaration.

```endo
# Write a builtin property
shell_prompt_preset <- "endo-signature"
agent_provider <- "claude"

# Read a builtin property (returns its current value)
print shell_prompt_preset
print agent_provider
```

Builtin properties behave like read/write variables but invoke getter/setter callbacks
internally. See [Shell Configuration](../shell/configuration.md) and
[Agent Configuration](../agent/configuration.md) for the full list of available properties.

### 4.4 Destructuring

Extract values from compound types directly in bindings.

<!-- endo-no-check -->
```endo
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
let greet { name; _ } = $"Hello, {name}"
let addPair (a, b) = a + b
let sumFirst [x; y; _...] = x + y
```

### 4.5 Scope and Visibility

<!-- endo-no-check -->
```endo
# Block scope with braces
let outer = 10
let result = {
    let inner = 20            # Only visible in this block
    inner + outer
}
# 'inner' is not visible here
println $"Result: {result}"      # Result: 30

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

### 4.6 Lazy Bindings

Use `lazy` to defer evaluation of an expression until its value is needed:

```endo
let x = lazy (1 + 2)
println (force x)
```

The expression `(1 + 2)` is not evaluated when `x` is bound. It is only computed when `force x` is called, and the result is cached for subsequent `force` calls.

See [Lazy Evaluation](lazy-evaluation.md) for full details.

---
**See also:** [Type System](type-system.md) | [Functions](functions.md) | [Pattern Matching](pattern-matching.md) | [Lazy Evaluation](lazy-evaluation.md)
