# Records

Demonstrates Endo's record types: defining custom types, creating values, field access, functional update, pattern matching, destructuring, and pipeline integration.

```endo
// Records: named product types with field access, update, and pattern matching

// Define a record type
type Person = { name: str; age: int }

// Create record values
let alice = { name = "Alice"; age = 30 }
let bob = { name = "Bob"; age = 25 }

// Field access
print "alice.name = "; println alice.name
print "alice.age = "; println alice.age
print "bob.name = "; println bob.name

// Field access in expressions
print "age sum = "; println (alice.age + bob.age)

// Record update (creates a new record, original unchanged)
let older_alice = { alice with age = 31 }
print "older_alice.age = "; println older_alice.age
print "alice.age still = "; println alice.age

// Update with expression
let next_birthday = { bob with age = bob.age + 1 }
print "bob next birthday age = "; println next_birthday.age

// Records passed to functions
let greet p = $"Hello, {p.name}!"
println (greet alice)
println (greet bob)

let is_adult p = p.age >= 18
print "alice is adult: "; println (is_adult alice)

// Record pattern matching
let get_name p =
    match p with
    | { name; age } -> name

print "get_name alice = "; println (get_name alice)

// Pattern matching with guards
let describe p =
    match p with
    | { name; age } when age >= 30 -> $"{name} is experienced"
    | { name; age } -> $"{name} is young"

println (describe alice)
println (describe bob)

// Record destructuring in let bindings
let { name; age } = alice
print "destructured name = "; println name
print "destructured age = "; println age

// Records in pipelines
let get_age p = p.age
print "alice age via pipeline = "; println (alice |> get_age)

// Print whole records
print "alice = "; println alice
print "bob = "; println bob
```

## Key Techniques

- **Type definitions** use `type Name = { field: type; ... }` to declare record types with named, typed fields.
- **Record creation** uses `{ field = value; ... }` syntax. Field order does not matter.
- **Dot-notation field access** (`record.field`) reads individual fields and can be used freely within expressions.
- **Functional update** (`{ record with field = newValue }`) creates a new record with one or more fields changed, leaving the original immutable.
- **Record pattern matching** destructures records in match arms with `| { field1; field2 } -> body`, binding each field to a local variable.
- **Guards on record patterns** use `when` conditions to refine matches based on field values.
- **Let destructuring** (`let { name; age } = record`) extracts fields into local variables without a full match expression.
- **String interpolation** with `$"..."` and `{expr}` embeds record field values directly into strings.
- Records integrate naturally with **functions and pipelines**, making them a cornerstone for structured data handling.

## See Also

- [Type System](../language/type-system.md)
- [Pattern Matching](../language/pattern-matching.md)
- [Variables & Bindings](../language/variables-and-bindings.md)
