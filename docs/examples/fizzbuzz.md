# FizzBuzz

The classic FizzBuzz problem solved in Endo using let-in bindings and guarded match arms.

```endo
// Classic FizzBuzz using let-in bindings and guarded match arms

let fizzbuzz n =
    let by3 = n % 3 in
    let by5 = n % 5 in
    match 0 with
    | _ when by3 == 0 && by5 == 0 -> "FizzBuzz"
    | _ when by3 == 0 -> "Fizz"
    | _ when by5 == 0 -> "Buzz"
    | _ -> string_of_int n

println (fizzbuzz 1)
println (fizzbuzz 2)
println (fizzbuzz 3)
println (fizzbuzz 4)
println (fizzbuzz 5)
println (fizzbuzz 6)
println (fizzbuzz 7)
println (fizzbuzz 10)
println (fizzbuzz 15)
println (fizzbuzz 30)
```

## Key Techniques

- **Let-in bindings** (`let by3 = n % 3 in ...`) precompute intermediate values in a scoped expression. The bindings `by3` and `by5` are only visible within the match body.
- **Guard-only matching** -- the match scrutinee is a dummy value (`0`), and all logic lives in `when` guards. This is a common pattern when decisions depend on multiple conditions rather than a single value's structure.
- **Guard ordering** matters: the `by3 == 0 && by5 == 0` arm must come first because match arms are evaluated top-to-bottom, and the combined case must be checked before the individual ones.
- **`string_of_int`** converts an integer to its string representation, used here for the default case where the number is neither Fizz nor Buzz.
- This example shows that even simple programming exercises benefit from Endo's concise functional style -- the entire solution is a single function with no mutable state.

## See Also

- [Pattern Matching](../language/pattern-matching.md)
- [Variables & Bindings](../language/variables-and-bindings.md)
- [Functions](../language/functions.md)
