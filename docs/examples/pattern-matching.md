# Pattern Matching

Demonstrates Endo's pattern matching with literal patterns, wildcards, variable bindings, guards, or-patterns, as-patterns, and boolean patterns.

```endo
// Pattern matching with literals, wildcards, guards, and or-patterns

// Matching on literal values
let describe n =
    match n with
    | 0 -> "zero"
    | 1 -> "one"
    | 2 -> "two"
    | _ -> "many"

println (describe 0)
println (describe 1)
println (describe 42)

// Wildcard always matches
let always_ten x =
    match x with
    | _ -> 10
println (always_ten 999)

// Binding the matched value
let doubled x =
    match x with
    | n -> n * 2
println (doubled 7)

// Guards refine pattern matches with conditions
let classify n =
    match n with
    | x when x > 0 -> "positive"
    | x when x < 0 -> "negative"
    | _ -> "zero"

println (classify 5)
println (classify (0 - 3))
println (classify 0)

// Chained guards for ranges
let grade score =
    match score with
    | s when s >= 90 -> "A"
    | s when s >= 80 -> "B"
    | s when s >= 70 -> "C"
    | _ -> "F"

println (grade 95)
println (grade 85)
println (grade 60)

// Or-patterns: multiple values sharing one arm
let is_weekend day =
    match day with
    | 6 | 7 -> "weekend"
    | _ -> "weekday"

println (is_weekend 6)
println (is_weekend 3)

let small_or_big n =
    match n with
    | 1 | 2 | 3 -> "small"
    | _ -> "big"

println (small_or_big 2)
println (small_or_big 99)

// As-patterns: bind while matching
let echo x =
    match x with
    | n as val -> val
println (echo 42)

// Boolean patterns
let bool_to_int b =
    match b with
    | true -> 1
    | false -> 0

println (bool_to_int true)
println (bool_to_int false)
```

## Key Techniques

- **Literal patterns** match exact values (`0`, `1`, `2`). They are checked in order, and the first matching arm wins.
- **Wildcard patterns** (`_`) match any value and are typically used as a catch-all in the final arm.
- **Variable binding patterns** (`n`) bind the matched value to a name for use in the arm body.
- **Guards** (`when condition`) add boolean conditions to patterns, enabling range checks and other complex logic without nested if-then-else.
- **Or-patterns** (`| 6 | 7 ->`) let multiple values share the same arm body, reducing repetition.
- **As-patterns** (`n as val`) bind the matched value to a name while simultaneously matching a sub-pattern.
- **Boolean patterns** match `true` and `false` directly, which is useful for converting booleans into other types.
- Match expressions are **exhaustive by convention** -- always include a wildcard or catch-all arm to handle unexpected values.

## See Also

- [Pattern Matching](../language/pattern-matching.md)
- [Control Flow](../language/control-flow.md)
