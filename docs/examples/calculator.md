# Calculator

A simple calculator that combines functions, pattern matching, partial application, pipelines, and tail recursion into a cohesive example.

```endo
// A simple calculator using functions and pattern matching

let add x y = x + y
let sub x y = x - y
let mul x y = x * y
let div x y =
    if y == 0 then 0
    else x / y

// Dispatch operations by code using match
let apply op x y =
    match op with
    | 1 -> add x y
    | 2 -> sub x y
    | 3 -> mul x y
    | 4 -> div x y
    | _ -> 0

print "10 + 5 = "; println (apply 1 10 5)
print "10 - 5 = "; println (apply 2 10 5)
print "10 * 5 = "; println (apply 3 10 5)
print "10 / 5 = "; println (apply 4 10 5)
print "10 / 0 = "; println (apply 4 10 0)

// Partial application
let add10 = add 10
let double = mul 2
let square x = x * x

print "add10 5 = "; println (add10 5)
print "double 7 = "; println (double 7)
print "square 9 = "; println (square 9)

// Pipelines
print "5 |> double |> add10 = "; println (5 |> double |> add10)
print "3 |> square |> double = "; println (3 |> square |> double)

// Tail-recursive accumulator for summing 1..n
let rec accumulate n acc =
    match n with
    | 0 -> acc
    | _ -> accumulate (n - 1) (acc + n)

print "sum 1..10 = "; println (accumulate 10 0)
```

## Key Techniques

- **Operation dispatch** uses pattern matching on an integer code to select which function to call, demonstrating a common dispatch-table pattern.
- **Division safety** is handled with an `if-then-else` guard that returns `0` when the divisor is zero, preventing runtime errors.
- **Partial application** creates specialized functions from general ones: `add 10` becomes `add10`, `mul 2` becomes `double`.
- **Pipeline composition** chains transformations left to right: `5 |> double |> add10` reads as "take 5, double it, then add 10."
- **Tail-recursive accumulation** sums a range without stack overflow by passing the running total as an accumulator parameter.
- This example illustrates how Endo's features compose naturally -- functions, pattern matching, partial application, and pipelines work together seamlessly.

## See Also

- [Functions](../language/functions.md)
- [Pattern Matching](../language/pattern-matching.md)
- [Operators & Pipelines](../language/operators-and-pipelines.md)
