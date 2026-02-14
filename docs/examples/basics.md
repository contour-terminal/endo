# Basics

An introduction to Endo's fundamental building blocks: variable bindings, arithmetic, strings, booleans, and mutable variables.

```endo
// Basic arithmetic and variable bindings

let x = 42
let y = 10
let sum = x + y
println sum

// Operator precedence
let a = 2 + 3 * 4
println a

let b = (2 + 3) * 4
println b

let c = 17 % 5
println c

let d = 2 ** 10
println d

// String concatenation
let greeting = "hello" + " world"
println greeting

// Auto-coercion in string concatenation
let msg = "count: " + 42
println msg

// Boolean values and operators
let flag = true
println flag

let negated = !true
println negated

let both = true && false
println both

let either = true || false
println either

// Chained let bindings
let p = 1
let q = 2
let r = p + q
println r

// Mutable variables with <- assignment
let mut counter = 0
counter <- counter + 1
counter <- counter + 1
counter <- counter + 1
println counter
```

## Key Techniques

- **`let` bindings** create immutable variables by default. Once bound, a value cannot be reassigned.
- **Arithmetic operators** follow standard precedence: `**` (exponentiation) binds tighter than `*`, `/`, `%`, which bind tighter than `+` and `-`. Parentheses override precedence as expected.
- **String concatenation** uses the `+` operator. When one operand is a string and the other is a number, Endo automatically coerces the number to a string.
- **Boolean operators** include `&&` (logical and), `||` (logical or), and `!` (logical negation). Boolean literals are `true` and `false`.
- **Mutable variables** are declared with `let mut` and reassigned with the `<-` operator. Use mutability sparingly -- immutable bindings are preferred in functional style.

## See Also

- [Variables & Bindings](../language/variables-and-bindings.md)
- [Lexical Elements](../language/lexical-elements.md)
- [Operators & Pipelines](../language/operators-and-pipelines.md)
