# Endo Language — F# Feature Implementation Status

This document tracks the implementation status of F# language features as defined in `LANGUAGE.md`.

**Legend:** [x] Implemented | [~] Partial | [ ] Not yet implemented

---

## Expressions

- [x] Immutable bindings: `let x = 42`
- [x] Mutable bindings: `let mut x = 0`
- [x] Mutation operator: `x <- x + 1`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Let-in expressions: `let x = 5 in x + 1`
- [x] If-then-else expressions: `if cond then a else b`
- [x] Match expressions: `match x with | pattern -> result`
- [ ] List expressions: `[1; 2; 3]` (parsed, no runtime)
- [ ] List ranges: `[1..10]` (parsed, no runtime)
- [ ] List comprehensions: `[for x in items -> expr]` (parsed, no runtime)
- [ ] Record expressions: `{ name = "Alice"; age = 30 }`
- [ ] Record update: `{ alice with age = 31 }`
- [x] Tuple expressions: `(1, "hello")` (2 and 3 elements)
- [ ] Tuple destructuring in let: `let (x, y) = tuple`
- [ ] Record destructuring in let: `let { name; age } = person`
- [ ] Block scopes: `{ let inner = 20; inner + outer }`

## Types

### Primitive Types
- [x] `int` — 64-bit signed integer
- [x] `float` — 64-bit floating point (via `std::bit_cast<uint64_t>(double)` in VM stack)
- [x] `str` — UTF-8 string
- [x] `bool` — Boolean
- [ ] `unit` — No value (void)

### Compound Types
- [ ] Lists: `list<int>` (parsed, no runtime representation)
- [x] Tuples: `(int, str)` (2 and 3 elements via TypedObject)
- [x] Options: `option<T>` with `Some` and `None`
- [x] Results: `result<T, E>` with `Ok` and `Error`
- [ ] Records: `type Person = { name: str; age: int }`
- [ ] Discriminated Unions: `type Shape = | Circle of float | Rectangle of float * float`
- [ ] Generic types

### Type Annotations
- [x] Variable annotations: `let count: int = 42`
- [x] Function parameter annotations: `let add (x: int) (y: int): int = x + y`
- [x] Lambda annotations: `fun (x: int) -> x + 1`

## Functions

- [x] Single-parameter functions: `let double x = x * 2`
- [x] Multi-parameter (curried) functions: `let add x y = x + y`
- [x] Partial application: `let add5 = add 5`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Multi-parameter lambdas: `fun x y -> x + y`
- [x] Closures (capturing outer scope variables)
- [x] Recursive functions: `let rec gcd a b = ...`
- [x] Mutual recursion: `let rec isEven n = ... and isOdd n = ...`
- [x] Tail-call optimization
- [ ] Function composition: `>>` and `<<` operators
- [x] Type-annotated functions

## Lists & Collections

- [ ] List literal construction: `[1; 2; 3]` (parsed, no runtime)
- [ ] List ranges: `[1..10]` (parsed, no runtime)
- [ ] Cons operator: `::`
- [ ] List concatenation: `@`
- [ ] List comprehensions with `when` filter
- [ ] Standard list operations (`map`, `filter`, `fold`, etc.)

## Pattern Matching

### Basic Patterns
- [x] Literal patterns: `| 0 -> "zero"`, `| "hello" -> ...`
- [x] Variable binding: `| n -> n + 1`
- [x] Wildcard: `| _ -> "default"`
- [x] Boolean patterns: `| true -> ... | false -> ...`

### Compound Patterns
- [x] Tuple patterns: `| (a, b) -> a + b`
- [ ] List patterns: `| [] -> ... | [x] -> ... | head :: tail -> ...`
- [ ] Record patterns: `| { name; age } -> ...`
- [x] Constructor patterns (Option): `| Some x -> ... | None -> ...`
- [x] Constructor patterns (Result): `| Ok v -> ... | Error e -> ...`

### Advanced Patterns
- [x] Or-patterns: `| 1 | 2 | 3 -> "small"`
- [x] As-patterns: `| n as val -> ...`
- [x] Guards (when clauses): `| x when x > 0 -> "positive"`
- [ ] Nested record patterns
- [ ] Nested list patterns

## Operators

### Arithmetic
- [x] `+`, `-`, `*`, `/`, `%`, `**`
- [x] Unary negation: `-x`

### Comparison
- [x] `==`, `!=`, `<`, `<=`, `>`, `>=`
- [x] Dynamic comparison for values from pattern matching (VCMPEQ etc.)

### Logical
- [x] `&&` (AND, short-circuit)
- [x] `||` (OR, short-circuit)
- [x] `!` (NOT)

### String
- [x] Concatenation: `"hello" + " world"`
- [x] Mixed type concatenation: `"count: " + 42`
- [x] F#-style interpolated strings: `$"Hello, {name}"`
- [ ] Repetition: `"ha" * 3`

### Pipe Operators
- [x] Forward pipe: `|>` (data |> func)
- [x] Shell pipe: `|` (cmd1 | cmd2)

### Composition
- [ ] Forward: `>>`
- [ ] Backward: `<<`

### List
- [ ] Cons: `::`
- [ ] Concatenation: `@`

### Special
- [x] Error propagation: `?`
- [ ] Optional chaining: `?.`
- [ ] Option default: `?|`

## Error Handling

- [x] Result type: `Ok value`, `Error msg`
- [x] Option type: `Some value`, `None`
- [x] Error propagation: `expr?` (with auto-wrapping for type-consistent returns)
- [x] Pattern matching on `?`-returning functions: `match (f x) with | Ok n -> n | Error e -> e`
- [x] `?` inside `let-in` expressions: `let f x = let v = (g x)? in v * 2`
- [x] Try-with expression: `try expr with | Error e -> handler`
- [x] Pattern matching on errors
- [x] Try-finally: `try ... finally cleanup`
- [ ] Option combinators: `.map()`, `.bind()`, `.defaultValue()`

## Control Flow

- [x] If-then-else expressions (F# style, returns value)
- [x] If-then-elif-else-fi statements (bash style)
- [x] Match expressions
- [x] While loops
- [x] For-in loops
- [x] Break and continue
- [ ] For loops with destructuring: `for (name, value) in entries do ... done`

## Standard Library Builtins

- [x] `print` — print without newline
- [x] `println` — print with newline
- [x] `fst` / `snd` — user-definable via pattern matching (no longer builtins)
- [x] `string_length` — length of string
- [x] `int_of_string` — string to integer conversion
- [x] `string_of_int` — integer to string conversion
- [x] `not` — boolean negation
- [ ] `head`, `tail`, `length`, `isEmpty` — list operations
- [ ] `map`, `filter`, `fold`, `reduce` — higher-order list functions
- [ ] `sort`, `reverse`, `distinct` — list transformations
- [ ] `String.split`, `String.join`, `String.trim` — string operations
- [ ] `File.read`, `File.write`, `File.list` — file operations
- [ ] `Path.join`, `Path.extension`, `Path.basename` — path operations

## Shell Integration

- [x] Shell command expressions: `& git status`
- [x] Command substitution: `$(cmd)`
- [x] Variable substitution: `$VAR`, `${VAR}`
- [x] String interpolation in double-quoted strings
- [x] Process substitution: `<(cmd)`, `>(cmd)`
- [x] Redirections: `>`, `>>`, `<`, `2>&1`
- [x] Here-strings: `<<<`
- [x] Job management: `&`, `jobs`, `fg`, `bg`

## Modules & Imports

- [ ] `import` statements
- [ ] `from ... import` statements
- [ ] Module-qualified access: `List.map`, `String.split`
- [ ] Module creation and exports

## Lexer / Parser

- [x] Context-sensitive tokenization (F# mode vs shell mode)
- [x] F# operator tokens: `+`, `-`, `*`, `/`, `%`, `**`, `|>`, `->`, `<-`
- [x] Nested parentheses in F# expressions (fixed: `))` no longer merges to `DblRndClose`)
- [x] `in`, `then`, `else` excluded from `isFSharpPrimary()` to prevent argument consumption
- [x] Comma tokenization in F# mode for tuples
- [x] Negative number literals: `-42`
- [x] Float literals with decimal: `3.14` (distinct `Float` type with arithmetic, comparisons, and promotion)
- [x] Hexadecimal: `0xFF`
- [x] Octal: `0o755`
- [x] Binary: `0b1010`
- [x] Scientific notation: `1e10`
- [x] Comments: `#`, `//`, `(* ... *)`

## REPL

- [x] Persist function definitions across REPL prompts
- [x] Persist recursive and mutual-recursive functions
- [x] Persist simple value bindings (`let x = 42`)
- [ ] Persist closure captures from previous prompts
