# Type Annotations

Demonstrates Endo's optional type annotations on variable bindings, function parameters, return types, lambdas, and generic types like `option` and `result`.

```endo
// Type annotations for variables, function parameters, and return types

// Annotated variable bindings
let x: int = 42
println x

let name: str = "Alice"
println name

let pi: float = 3.14159
println pi

let active: bool = true
println active

// Function with annotated parameters and return type
let add (x: int) (y: int): int = x + y
println (add 3 4)

// Mixed annotated and bare parameters
let scale (factor: int) n = factor * n
println (scale 3 7)

// Return type annotation only
let square x: int = x * x
println (square 6)

// Annotated lambda expressions
let double = fun (x: int) -> x * 2
println (double 5)

let greet = fun (name: str) -> "Hello, " + name
println (greet "Bob")

// Multi-parameter annotated lambda
let mul = fun (x: int) (y: int) -> x * y
println (mul 4 5)

// Function type annotation on binding
let inc: int -> int = fun x -> x + 1
println (inc 9)

// Partial application preserves type annotations
let add10 = add 10
println (add10 20)

// Recursive function with annotations
let rec factorial (n: int) (acc: int): int =
    match n with
    | 0 -> acc
    | _ -> factorial (n - 1) (acc * n)
println (factorial 6 1)

// Annotated let-in expressions
let result = let a: int = 10 in a * a
println result

// Pipelines with annotated functions
let negate (x: int): int = 0 - x
println (5 |> double |> negate)

// Option and result type annotations
let maybe: option<int> = Some 42
let m = match maybe with | Some v -> v | None -> 0
println m

let ok_val: result<int, str> = Ok 100
let r = match ok_val with | Ok v -> v | Error _ -> 0
println r
```

## Key Techniques

- **Variable type annotations** use `let name: type = value` syntax. Supported base types are `int`, `str`, `float`, and `bool`.
- **Parameter annotations** wrap each parameter in parentheses: `(x: int)`. Annotated and bare parameters can be mixed freely.
- **Return type annotations** appear after the parameter list: `let add (x: int) (y: int): int = ...`.
- **Lambda annotations** follow the same pattern: `fun (x: int) -> body`.
- **Function type annotations** on bindings use arrow syntax: `let inc: int -> int = fun x -> x + 1`.
- **Generic type annotations** support parameterized types like `option<int>` and `result<int, str>`.
- **Type annotations are optional** -- Endo infers types automatically. Annotations serve as documentation and can catch type mismatches early.
- All annotated functions work seamlessly with **partial application** and **pipelines**, just like unannotated functions.

## See Also

- [Type System](../language/type-system.md)
- [Functions](../language/functions.md)
- [Variables & Bindings](../language/variables-and-bindings.md)
