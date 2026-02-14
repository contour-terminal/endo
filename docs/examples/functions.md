# Functions

A tour of Endo's function features: named functions, lambdas, closures, partial application, pipelines, and let-in expressions.

```endo
// Functions, lambdas, closures, partial application, and pipelines

// Named functions
let double x = x * 2
println (double 5)

let add x y = x + y
println (add 3 4)

let compute x y = (x + y) * 2
println (compute 3 4)

// Lambda expressions (anonymous functions)
let triple = fun x -> x * 3
println (triple 5)

let result = (fun x y -> x + y) 3 4
println result

// Closures capture variables from enclosing scope
let multiplier = 10
let scale = fun x -> x * multiplier
println (scale 7)

let offset = 100
let addOffset x = x + offset
println (addOffset 5)

// Partial application (currying)
let add5 = add 5
println (add5 10)

let mul x y z = x * y * z
let mul2 = mul 2
let mul2x3 = mul2 3
println (mul2x3 4)

// Pipelines: value |> function
println (5 |> double)

let inc x = x + 1
println (5 |> double |> inc)

println (10 |> add 5)

// Pipelines with inline lambdas
println (5 |> (fun x -> x * 2) |> (fun x -> x + 1))

// Let-in expressions (scoped bindings)
println (let x = 7 in x * 3)

let r =
    let a = 1 in
    let b = 2 in
    a + b
println r

// Functions as values
let f = add
println (f 3 4)
```

## Key Techniques

- **Named functions** are defined with `let name params = body`. Multi-parameter functions are curried automatically.
- **Lambda expressions** use `fun params -> body` syntax, creating anonymous functions that can be assigned to variables or passed inline.
- **Closures** capture variables from their enclosing scope. Both lambdas and named functions can close over outer bindings.
- **Partial application** applies fewer arguments than a function expects, returning a new function that takes the remaining arguments. This is a natural consequence of currying.
- **Pipelines** (`|>`) pass the left-hand value as the last argument to the right-hand function, enabling a left-to-right data flow style. Pipelines chain naturally: `5 |> double |> inc`.
- **Let-in expressions** create scoped bindings that are only visible within the `in` body. They can be nested for multi-step computations.
- **Functions as values** -- functions are first-class citizens and can be assigned to variables, passed as arguments, and returned from other functions.

## See Also

- [Functions](../language/functions.md)
- [Operators & Pipelines](../language/operators-and-pipelines.md)
- [Variables & Bindings](../language/variables-and-bindings.md)
